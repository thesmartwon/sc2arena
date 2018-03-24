#include <arena.h>
#include <arena_types.h>
#include <time.h>

using namespace std;

Arena::Arena(vector<Bot> bots, vector<string> maps)
{
	this->bots = bots;
	num_players = bots.size();
	this->maps = maps;
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

bool make_proc(string cmd, vector<string> args) {

	string full_cmd = "";
	for (const auto &s : args) cmd += s;
	cout << "Starting " << full_cmd << endl;

	FILE* in = _popen(cmd.c_str(), "r");

	if (in == NULL) {
		const string msg = "Couldn't make process for " + cmd + ".";
		cerr << msg << endl;
		return false;
	}

	char line[256];
	while (fgets(line, sizeof(line), in) != NULL) {
		// TODO: log stdout to file
	}

	return true;
}

ClientStatus client_tick(sc2::Connection* client, sc2::Server* server) {
	auto client_status = ClientStatus::Running;
	clock_t last_request = clock();

	while (client_status == ClientStatus::Running) {
		if (server->HasRequest()) {
			const auto request = server->PeekRequest();
			if (request.second->has_quit()) {
				client_status = ClientStatus::Quit;
				// Intercept leave game and quit requests, we want to keep game alive to save replays
				break;
			}

			if (client->connection_ != nullptr) {
				server->SendRequest(client->connection_);
			}

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
			}

			// Send the response back to the client.
			if (server->connections_.size() > 0) {
				server->QueueResponse(client->connection_, response);
				server->SendResponse();
			}
			else {
				client_status = ClientStatus::ClientTimeout;
			}
			last_request = clock();
		}
		else {
			if ((last_request + (50 * CLOCKS_PER_SEC)) < clock()) {
				cout << "Client timeout in loop" << endl;
				client_status = ClientStatus::ClientTimeout;
			}
		}
		this_thread::sleep_for(std::chrono::nanoseconds(12ns));
	}

	return client_status;
}

void Arena::start_sc2(int port, int argc, char* argv[]) {
	// 1. Launch two instances of the SC2 client.
	// Setup sc2api websocket server
	for (int i = 0; i < num_players; i++) {
		sc2::Server* s = new sc2::Server();
		servers.push_back(s);
		cout << "Server port:" << port << endl;
		s->Listen(to_string(port++).c_str(), REQUEST_TIMEOUT, REQUEST_TIMEOUT, GAME_THREADS);
	}

	// Run sc2 executable to connect server
	sc2::ParseSettings(argc, argv, process_settings, game_settings);

	sc2_pids = vector<uint64_t>(num_players);
	for (int i = 0; i < num_players; i++) {
		cout << "SC2 port:" << port << endl;
		sc2_pids[i] = sc2::StartProcess(process_settings.process_path, {
			"-listen", BOT_HOST,
			"-port", to_string(port++).c_str(),
			"-displayMode", "0",
			"-dataVersion", process_settings.data_version });
	}
	cout << "Starting " << num_players << " instances of sc2 ";
	cout << "(timeout " << GAME_TIMEOUT << ")...";
	// Give game 10 sec to start
	sc2::SleepFor(GAME_TIMEOUT);
	cout << "Started" << endl;
}

void Arena::connect_players(int port, string first_map, string proc_path) {
	// Connect to localhost websocket
	for (int i = 0; i < num_players; i++) {
		cout << "Connecting port:" << port << endl;
		auto con = new sc2::Connection();
		con->Connect(BOT_HOST, port++);
		connections.push_back(con);
	}

	// 2. Choose one of the instances to act as the game host (clients[0])
	// 3. Send RequestCreateGame to the host with a valid multi player map.
	sc2::ProtoInterface proto;
	sc2::GameRequestPtr req = proto.MakeRequest();
	SC2APIProtocol::RequestCreateGame* game_request = req->mutable_create_game();
	resolve_map(game_request, first_map, proc_path);
	game_request->set_realtime(false);

	// 4. Send RequestJoinGame to both of the clients with the desired player config.
	// Might as well inform it of the players... we already know them
	for (auto const &p : bots) {
		// TODO: What is so special about the PlayerSetup* they return to us?
		SC2APIProtocol::PlayerSetup* playerSetup = game_request->add_player_setup();
		playerSetup->set_type(SC2APIProtocol::PlayerType(p.type));
		playerSetup->set_race(SC2APIProtocol::Race(int(p.race) + 1));
		playerSetup->set_difficulty(SC2APIProtocol::Difficulty(p.difficulty));
	}

	// Finish 3.
	connections[0]->Send(req.get());
	SC2APIProtocol::Response* create_response = nullptr;
	if (connections[0]->Receive(create_response, GAME_TIMEOUT)) {
		auto game_response = create_response->create_game();
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
			cout << "Recieved create game response " << create_response->data().DebugString() << endl;
		}
	}
}

//int count_running() {
//	int count = 0;
//
//	for (int i = 0; i < bot_procs.size(); i++) {
//		if (bot_procs[i]->running()) {
//			count++;
//		}
//	}
//
//	return count;
//}

int get_results(sc2::Connection* con) {
	return 0;
}

int Arena::run_bot_bins() {
	int res = ArenaResult::None;
	vector<future<ClientStatus>> threads_tick(num_players);

	// Start each bot's binary as a subprocess
	for (auto const &b : bots) {
		string full_cmd = b.path;
		for (const string &s : b.cmd_args) full_cmd += s;
		cout << "Spawning " << full_cmd << endl;
		
		try {
			// bot_procs.push_back(bp::child(full_cmd));
		}
		catch (exception e) {
			cerr << "Failed to start " << full_cmd << endl;
			cerr << e.what();
		}
	}
	

	// For each bot make a thread to handle the connection
	for (int i = 0; i < num_players; i++) {
		threads_tick[i] = async(&client_tick, connections[i], servers[i]);
	}

	cout << "Giving bots 5s to start...";
	this_thread::sleep_for(std::chrono::nanoseconds(5s));

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
				cout << "bot " << bots[i].name << " done\n";
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
	// Run the protocol...

	start_sc2(PORT_P1, argc, argv);
	connect_players(int(PORT_P1 + bots.size()), map, process_settings.process_path);

	return run_bot_bins();
}
