#include <iostream>
#include <string>

#include "FileWorker.h"
#include "GameWorker.h"

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

// Настраивает кодировку консоли Windows для вывода русского текста.
static void SetupConsole() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

// Выводит приветствие перед началом игры.
static void PrintWelcome() {
    std::cout << "==============================================\n"
        << "   ХАММУРАПИ - правитель древнего Вавилона\n"
        << "==============================================\n"
        << "Вам предстоит управлять городом: покупать и продавать землю,\n"
        << "засевать поля и следить, чтобы народ не голодал.\n"
        << "Да пребудет с вами мудрость!\n\n";
}

int main() {
    const std::string savePath = "save.json";
    const GameConfig cfg{};

    SetupConsole();
    PrintWelcome();

    const LoadResult loaded = LoadGame(savePath, cfg);

    if (loaded.status == LoadStatus::Corrupted) {
        std::cout << "Внимание: файл сохранения " << savePath << " повреждён, не читается или содержит\n"
            << "недопустимые данные, поэтому прошлую игру продолжить нельзя.\n"
            << "Начинаем новую игру; повреждённый файл будет перезаписан при первом автосохранении.\n\n";
    }

    GameParameters game;

    if (loaded.game && AskResumeSavedGame(loaded.game->round)) {
        game = *loaded.game;
    }
    else {
        game = CreateNewGame(cfg);
    }

    RunGame(game, cfg, savePath);
    return 0;
}