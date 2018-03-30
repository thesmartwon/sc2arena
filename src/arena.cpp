#include <arena.h>
#include <iostream>
#include <time.h>
#include <vector>
#include <string>
#include <future>
#include <sc2api/sc2_args.h>
#include <sc2api/sc2_game_settings.h>
#include <sc2api/sc2_connection.h>
#include <sc2utils/sc2_manage_process.h>
#include <arena_process.h>

using namespace std;

void Arena::init(vector<Bot> bots, vector<string> maps) {
	Arena::bots = bots;
	num_players = bots.size();
	Arena::maps = maps;
	num_maps = maps.size();
}

void Arena::resolve_map(SC2APIProtocol::RequestCreateGame* request, string map_name, string proc_path) {
	// BattleNet map
	if (!sc2::HasExtension(map_name, ".SC2Map")) {
		request->set_battlenet_map_name(map_name);
		return;
	}

	// Absolute path
	SC2APIProtocol::LocalMap* local_map = request->mutable_local_map();
	if (sc2::DoesFileExist(map_name)) {
		local_map->set_map_path(map_name);
		return;
	}

	// Relative path - Game maps directory
	string game_relative = sc2::GetGameMapsDirectory(proc_path) + map_name;
	if (sc2::DoesFileExist(game_relative)) {
		local_map->set_map_path(map_name);
		return;
	}

	// Relative path - Library maps directory
	string library_relative = sc2::GetLibraryMapsDirectory() + map_name;
	if (sc2::DoesFileExist(library_relative)) {
		local_map->set_map_path(library_relative);
		return;
	}

	// Relative path - Remotely saved maps directory
	local_map->set_map_path(map_name);
}

ClientStatus client_tick(sc2::Connection* client, sc2::Server* server) {
	auto client_status = ClientStatus::Running;
	clock_t last_request = clock();

	while (client_status == ClientStatus::Running) {
		if (server->HasRequest()) {
			if (server->PeekRequest().second->has_quit()) {
				client_status = ClientStatus::Quit;
				// Intercept leave game and quit requests, we want to keep game alive to save replays
				break;
			}
			else if (server->connections_.size() <= 0 || client->connection_ == nullptr) {
				client_status = ClientStatus::ClientTimeout;
				break;
			}

			server->SendRequest(client->connection_);

			// Block for sc2's response then queue it.
			SC2APIProtocol::Response* response = nullptr;
			client->Receive(response, GAME_TIMEOUT);
			if (response != nullptr) {
				auto status = response->status();
				if (status > SC2APIProtocol::Status::in_replay) {
					client_status = ClientStatus::GameEnd;
				}
				if (response->has_observation()) {
					const auto obs = response->observation().observation();
					if (obs.game_loop() > ARENA_GAME_TIMEOUT) {
						client_status = ClientStatus::GameTimeout;
					}
				}

				// Send the response back to the client.
				server->QueueResponse(client->connection_, response);
				server->SendResponse();
			}
			else {
				cout << "Null response dammit\n";
			}



			last_request = clock();
		}
		//else if ((last_request + (50 * CLOCKS_PER_SEC)) < clock()) {
		//	cout << "Client timeout in loop" << endl;
		//	client_status = ClientStatus::ClientTimeout;
		//}
		this_thread::sleep_for(std::chrono::nanoseconds(12ns));
	}

	return client_status;
}

void Arena::start_sc2(int port, int argc, char* argv[]) {
	// 1) Launch two game instances with separate ports.
	// Setup sc2api websocket server
	for (int i = 0; i < num_players; i++) {
		sc2::Server* s = new sc2::Server();
		servers.push_back(s);
		cout << "Player " << i << ", port:" << port << endl;
		s->Listen(to_string(port++).c_str(), REQUEST_TIMEOUT, REQUEST_TIMEOUT, GAME_THREADS);
	}

	cout << "Starting " << num_players << " instances of sc2 "
		<< "(timeout " << GAME_TIMEOUT << ")..." << endl;
	sc2::ParseSettings(argc, argv, process_settings, game_settings);
	pids.reserve(num_players);
	for (int i = 0; i < num_players; i++) {
		pids[i] = sc2::StartProcess(process_settings.process_path, {
			"-listen", BOT_HOST,
			"-port", to_string(port++).c_str(),
			"-displayMode", "0",
			"-dataVersion", process_settings.data_version });
		cout << "Starting SC2, PID:" << pids[i]
			<< ", port:" << port << endl;
	}

	// Give game 10 sec to start
	sc2::SleepFor(GAME_TIMEOUT);
}

void Arena::connect_players(int port, string first_map, string proc_path) {
	// Connect to localhost websocket
	for (int i = 0; i < num_players; i++) {
		cout << "Player " << i << ", connecting port:" << port << endl;
		connections.push_back(new sc2::Connection());
		connections.back()->Connect(BOT_HOST, port++);
	}

	// 2) Designate a host, and Request.create_game with a multiplayer map.
	sc2::GameRequestPtr req = proto.MakeRequest();
	SC2APIProtocol::RequestCreateGame* game_request = req->mutable_create_game();
	resolve_map(game_request, first_map, proc_path);
	game_request->set_realtime(false);

	for (auto const &p : bots) {
		SC2APIProtocol::PlayerSetup* playerSetup = game_request->add_player_setup();
		playerSetup->set_type(SC2APIProtocol::PlayerType(p.type));
		playerSetup->set_race(SC2APIProtocol::Race(int(p.race) + 1));
		playerSetup->set_difficulty(SC2APIProtocol::Difficulty(p.difficulty));
	}

	connections[0]->Send(req.get());
	SC2APIProtocol::Response* response_create_game = nullptr;
	if (connections[0]->Receive(response_create_game, GAME_TIMEOUT)) {
		auto game_response = response_create_game->create_game();
		if (game_response.has_error()) {
			string errorCode = "Unknown";
			switch (game_response.error()) {
			case SC2APIProtocol::ResponseCreateGame::MissingMap: errorCode = "Missing Map";
			case SC2APIProtocol::ResponseCreateGame::InvalidMapPath: errorCode = "Invalid Map Path";
			case SC2APIProtocol::ResponseCreateGame::InvalidMapData: errorCode = "Invalid Map Data";
			case SC2APIProtocol::ResponseCreateGame::InvalidMapName: errorCode = "Invalid Map Name";
			case SC2APIProtocol::ResponseCreateGame::InvalidMapHandle: errorCode = "Invalid Map Handle";
			case SC2APIProtocol::ResponseCreateGame::MissingPlayerSetup: errorCode = "Missing Player Setup";
			case SC2APIProtocol::ResponseCreateGame::InvalidPlayerSetup: errorCode = "Invalid Player Setup";
			}

			cerr << "CreateGame request returned an error code: " << errorCode << endl;
			if (game_response.has_error_details() && game_response.error_details().length() > 0) {
				cerr << "CreateGame request returned error details: " << game_response.error_details() << endl;
			}
			exit(-1);
		}
		else {
			cout << "Recieved create game response " << response_create_game->data().DebugString() << endl;
		}
	}
	// 3) Call Request.join on BOTH clients.Join will block until both clients connect.
	// The client take over from here and do coordinator.JoinGame(); themselves


	
}

int get_results(sc2::Connection* con) {
	return 0;
}

int Arena::run_bot_bins() {
	// 4) Wait for a response from both clients.They can now play / step.
	int res = ArenaResult::None;
	vector<future<ClientStatus>> threads_tick(num_players);

	// Start each bot's binary as a subprocess
	for (auto const &b : bots) {
		pids.push_back(start_proc(b.path, b.cmd_args));
	}

	cout << "Giving bots 5s to join..." << endl;
	this_thread::sleep_for(std::chrono::nanoseconds(5s));
	cout << "Starting game" << endl;

	// For each bot make a thread to handle the connection
	for (int i = 0; i < num_players; i++) {
		threads_tick[i] = async(&client_tick, connections[i], servers[i]);
	}

	vector<future_status> statuses_tick(num_players);
	// Run them until game ends.
	while (true) {
		// Wait for first thread
		statuses_tick[0] = threads_tick[0].wait_for(1s);
		// Get status of the rest
		for (unsigned int i = 1; i < threads_tick.size(); i++) {
			statuses_tick[i] = threads_tick[i].wait_for(0s);
		}

		for (unsigned int i = 0; i < statuses_tick.size(); i++) {
			if (statuses_tick[i] == future_status::ready) {
				cout << "bot " << bots[i].name << " done" << endl;
				ClientStatus cs = threads_tick[i].get();
				switch (cs) {
				case ClientStatus::ClientTimeout:	res = res | (Player1Crash + (1u << i));
				case ClientStatus::Quit:			res = res | (Player1Forfeit + (1u << i));
				case ClientStatus::GameTimeout:		res = res | GameTimeout;
				}
			}
			//if (bot_procs[i].valid() && !bot_procs[i].running()) {
			//	cout << "bot " << bots[i].name << " crash ";
			//	cout << "(Exit code: " << bot_procs[i].exit_code() << ")" << endl;
			//	res = res | (Player1Crash + (1u << i));
			//	bot_procs[i].terminate(); // Makes invalid.
			//}
		}
	}
	// Get who won
	res = res | get_results(connections[0]);

	// Cleanup

	return res;
}

int Arena::play(string map, int argc, char* argv[]) {
	// Follow the instructions...
	// https://github.com/Blizzard/s2client-proto/blob/master/s2clientprotocol/sc2api.proto

	start_sc2(PORT_P1, argc, argv);
	connect_players(int(PORT_P1 + bots.size()), map, process_settings.process_path);

	return run_bot_bins();
}

//bool Arena::kill_procs() {
//	bool res = true;
//	for (uint64_t pid : pids)
//		if (!kill_proc(pid))
//			res = false;
//
//	return res;
//};
//
//void Arena::sig_handler() {
//	if (!kill_procs())
//		cout << "Failed to kill some subprocess" << endl;
//	else
//		cout << "Killed all subprocesses" << endl;
//
//	exit(1);
//};
