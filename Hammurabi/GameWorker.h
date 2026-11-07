#pragma once

#include <optional>
#include <string>

#include "GameTypes.h"

// Что именно не так с командами игрока
enum class CommandError {
    None,                     // Все хорошо
    Negative,                 // Введено отрицательно значение
    BuyAndSell,               // Одновременно покупка и продажа земли
    NotEnoughWheatForLand,    // Нехватка пшеницы на покупку земли
    NotEnoughLand,            // Продажа больше земли, чем есть
    NotEnoughWheatForSeeds    // Недостаточно пшеницы для посева
};

// Создает игру, формируя параметры игры
GameParameters CreateNewGame(const GameConfig& cfg);

// Запускает игру
GameStatus RunGame(GameParameters& game, const GameConfig& cfg, const std::string& savePath);

// Запускает один раунд игры
GameStatus RunOneRound(GameParameters& game, const UserCommands& commands, const GameConfig& cfg);

// Принимает команды от игрока
std::optional<UserCommands> GetUserCommands(const GameParameters& game);

// Проверяет команды от игрока по правилам игры
CommandError ValidateCommands(const GameParameters& game, const UserCommands& commands);

// Выводит результат раунда игры
void PrintRoundReport(const GameParameters& game);     

// Выводит результат игры
void PrintFinalResult(const GameParameters& game, const GameConfig& cfg);

// Запрашивает выход из игры
bool AskContinueOrExit();

// Запрашивает продолжение игры
bool AskResumeSavedGame(int savedRound);