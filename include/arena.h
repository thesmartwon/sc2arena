#pragma once
#include <iostream>
#include <vector>
#include <string>
#include <future>
#include <arena_types.h>
#include <sc2api/sc2_args.h>
#include <sc2api/sc2_game_settings.h>
#include <sc2api/sc2_connection.h>
#include <sc2api/sc2_proto_interface.h>
#include <sc2utils/sc2_manage_process.h>

using namespace std;

class Arena {
private:
	vector<Bot> bots;
	size_t num_players;
	vector<string> maps;
	size_t num_maps;

	sc2::ProcessSettings process_settings;
	sc2::GameSettings game_settings;
	vector<uint64_t> sc2_pids;
	vector<sc2::Connection*> connections;
	vector<sc2::Server*> servers;

	void resolve_map(SC2APIProtocol::RequestCreateGame* request, string map_name, string proc_path);
	void start_sc2(int port, int argc, char* argv[]);
	void connect_players(int port, string first_map, string proc_path);
	int run_bot_bins();
public:
	Arena(vector<Bot> bots, vector<string> maps);
	int play(string map, int argc, char *argv[]);
};