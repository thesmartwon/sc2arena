#pragma once
#define BOT_HOST "127.0.0.1"
#define PORT_P1 5677
#define PORT_GAME_START 5690
#define REQUEST_TIMEOUT "2000" // ms
#define GAME_TIMEOUT 10000 // ms
#define ARENA_GAME_TIMEOUT 20*60*60 // ms 
#define GAME_THREADS "4"

#include <sc2api/sc2_game_settings.h>
#include <sc2api/sc2_server.h>
#include <string> 

using namespace std;

struct Bot : sc2::PlayerSetup {
	string name;
	string path;
	vector<string> cmd_args;
	sc2::Race race;
	sc2::Difficulty difficulty;
	int seed;
};

struct SC2ProcSettings {
	sc2::ProcessSettings process_settings;
	sc2::GameSettings game_settings;
	vector<sc2::Server*> servers;
	vector<uint64_t> sc2_pids;
};
enum ClientStatus {
	GameEnd,
	GameTimeout,
	ClientTimeout,
	Quit,
	Running
};
enum ArenaResult {
	None = 0,
	Error = (1u << 0),
	Player1Win = (1u << 1),
	Player2Win = (1u << 2),
	Player3Win = (1u << 3),
	Player4Win = (1u << 4),
	Player5Win = (1u << 5),
	Player6Win = (1u << 6),
	Player7Win = (1u << 7),
	Player8Win = (1u << 8),
	Player1Crash = (1u << 9),
	Player2Crash = (1u << 10),
	Player3Crash = (1u << 11),
	Player4Crash = (1u << 12),
	Player5Crash = (1u << 13),
	Player6Crash = (1u << 14),
	Player7Crash = (1u << 15),
	Player8Crash = (1u << 16),
	Player1Forfeit = (1u << 17),
	Player2Forfeit = (1u << 18),
	Player3Forfeit = (1u << 19),
	Player4Forfeit = (1u << 20),
	Player5Forfeit = (1u << 21),
	Player6Forfeit = (1u << 22),
	Player7Forfeit = (1u << 23),
	Player8Forfeit = (1u << 24),
	Timeout = (1u << 25),
};

inline ArenaResult operator |(ArenaResult a, ArenaResult b) {
	return static_cast<ArenaResult>(static_cast<int>(a) | static_cast<int>(b));
}

inline ArenaResult operator &(ArenaResult a, ArenaResult b) {
	return static_cast<ArenaResult>(static_cast<int>(a) & static_cast<int>(b));
}
