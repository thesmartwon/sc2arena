#include <arena.h>
#include <tournament.h>

using namespace std;

static string race_string(sc2::Race race) {
	switch (race) {
	case sc2::Race::Protoss:	return "Protoss";
	case sc2::Race::Random:		return "Random";
	case sc2::Race::Terran:		return "Terran";
	case sc2::Race::Zerg:		return "Zerg";
	default:					return "Random";
	}
}

static string difficulty_string(sc2::Difficulty InDifficulty)
{
	switch (InDifficulty) {
	case sc2::Difficulty::VeryEasy:		return "VeryEasy";
	case sc2::Difficulty::Easy:			return "Easy";
	case sc2::Difficulty::Medium:		return "Medium";
	case sc2::Difficulty::MediumHard:	return "MediumHard";
	case sc2::Difficulty::Hard:			return "Hard";
	case sc2::Difficulty::HardVeryHard:	return "HardVeryHard";
	case sc2::Difficulty::VeryHard:		return "VeryHard";
	case sc2::Difficulty::CheatVision:	return "CheatVision";
	case sc2::Difficulty::CheatMoney:	return "CheatMoney";
	case sc2::Difficulty::CheatInsane:	return "CheatInsane";
	default:							return "Easy";
	}
}

static string result_string(ArenaResult res) {
	if (res & ArenaResult::Error) return "Error";

	string msg = "";
	if (res & ArenaResult::Timeout) {
		msg += "Timeout\n";
	}
	for (int i = ArenaResult::Player1Crash; i <= ArenaResult::Player8Crash; i++) {
		if (res & i) {
			msg += "Player" + to_string(i) + "Crash\n";
		}
	}
	for (int i = ArenaResult::Player1Forfeit; i <= ArenaResult::Player8Forfeit; i++) {
		if (res & i) {
			msg += "Player" + to_string(i) + "Forfeit\n";
		}
	}
	for (int i = ArenaResult::Player1Win; i <= ArenaResult::Player8Win; i++) {
		if (res & i) {
			msg += "Player" + to_string(i) + "Win\n";
		}
	}
}

static vector<string> make_args(const Bot &bot, int game_port, int start_port, bool is_computer) {
	vector<string> res;
	res.push_back(" --GamePort " + std::to_string(game_port));
	res.push_back(" --StartPort " + std::to_string(start_port));
	res.push_back(" --LadderServer 127.0.0.1");

	if (is_computer) {
		res.push_back(" --ComputerOpponent 1");
		res.push_back(" --ComputerRace " + race_string(bot.race));
		res.push_back(" --ComputerDifficulty " + difficulty_string(bot.difficulty));
	}

	return res;
}

int main(int argc, char* argv[]) {
	vector<Bot> bots;
	vector<string> maps;

	Bot b1, b2;
	b1.name = "5minBot";
	b1.path = "C:/dev/5minBot/bin/5minBot.exe";
	b1.cmd_args = make_args(b1, PORT_P1, PORT_GAME_START, false);
	b1.race = sc2::Terran;
	b1.type = sc2::PlayerType::Participant;
	b1.difficulty = sc2::Difficulty::Easy;

	b2.name = "CryptBot";
	b2.path = "C:/dev/CryptBot/x64/Release/CryptBot.exe";
	b2.cmd_args = make_args(b2, PORT_P1 + 1, PORT_GAME_START, false);
	b2.race = sc2::Protoss;
	b2.type = sc2::PlayerType::Participant;
	b2.difficulty = sc2::Difficulty::Easy;

	bots.push_back(b1);
	bots.push_back(b2);

	maps = { "AcolyteLE.SC2Map" };
	
	Arena arena(bots, maps);
	vector<int> results;
	TournamentType type;

	results.push_back(arena.play(maps[0], argc, argv));

	return 0;
}
