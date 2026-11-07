#include "GameWorker.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <string>

#include "FileWorker.h"

static constexpr double kEpsilon = 1e-6;
static constexpr double kArrivalsStarvedDivisor = 2.0;
static constexpr double kArrivalsBaseYield = 5.0;
static constexpr double kArrivalsWheatDivisor = 600.0;
static constexpr double kArrivalsBonus = 1.0;

// Округляет значение до двух знаков после запятой
static double Round2(double value) {
    const double rounded = std::round(value * 100.0) / 100.0;
    return rounded == 0.0 ? 0.0 : rounded;
}

// Возвращает генератор случайных чисел
static std::mt19937& Rng() {
    static std::mt19937 engine(std::random_device{}());
    return engine;
}

// Генерирует случайное целое число в заданном диапазоне
static int RandomInt(int low, int high) {
    return std::uniform_int_distribution<int>(low, high)(Rng());
}

// Генерирует случайное вещественное число в заданном диапазоне
static double RandomReal(double low, double high) {
    return std::uniform_real_distribution<double>(low, high)(Rng());
}

// Выбирает случайную цену земли
static int RollLandPrice(const GameConfig& cfg) {
    return RandomInt(cfg.priceMin, cfg.priceMax);
}

// Форматирует число, удаляя незначащие нули после запятой
static std::string FormatNumber(double value) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(2) << Round2(value);
    std::string result = output.str();

    while (!result.empty() && result.back() == '0') {
        result.pop_back();
    }

    if (!result.empty() && result.back() == '.') {
        result.pop_back();
    }

    return result == "-0" || result.empty() ? "0" : result;
}

// Считывает строку из консоли
static bool ReadLine(std::string& line) {
    return static_cast<bool>(std::getline(std::cin, line));
}

// Преобразует строку в целое число
static bool ParseInt(const std::string& text, int& out) {
    const char* begin = text.c_str();
    char* end = nullptr;

    errno = 0;
    const long long value = std::strtoll(begin, &end, 10);

    if (end == begin || errno == ERANGE) return false;

    while (*end != '\0' && std::isspace(static_cast<unsigned char>(*end))) {
        ++end;
    }

    if (*end != '\0' || value < INT_MIN || value > INT_MAX) {
        return false;
    }

    out = static_cast<int>(value);
    return true;
}

// Преобразует строку в вещественное число
static bool ParseDouble(const std::string& text, double& out) {
    std::string normalized = text;

    for (char& c : normalized) {
        if (c == ',') {
            c = '.';
        }
    }

    const char* begin = normalized.c_str();
    char* end = nullptr;

    errno = 0;
    const double value = std::strtod(begin, &end);

    if (end == begin || errno == ERANGE || !std::isfinite(value)) {
        return false;
    }

    while (*end != '\0' && std::isspace(static_cast<unsigned char>(*end))) {
        ++end;
    }

    if (*end != '\0') {
        return false;
    }

    out = value;
    return true;
}

// Запрашивает у игрока целое число
static bool PromptInt(const char* prompt, int& out) {
    std::string line;

    while (true) {
        std::cout << prompt;

        if (!ReadLine(line)) {
            std::cout << '\n';
            return false;
        }

        if (ParseInt(line, out)) {
            return true;
        }

        std::cout << "Повелитель, я не понял ответ. Нужно ввести целое число.\n";
    }
}

// Запрашивает у игрока вещественное число
static bool PromptDouble(const char* prompt, double& out) {
    std::string line;

    while (true) {
        std::cout << prompt;

        if (!ReadLine(line)) {
            std::cout << '\n';
            return false;
        }

        if (ParseDouble(line, out)) {
            return true;
        }

        std::cout << "Повелитель, я не понял ответ. Нужно ввести число.\n";
    }
}

// Вычисляет остаток пшеницы после покупки или продажи земли
static double WheatAfterTrade(const GameParameters& game, const UserCommands& commands) {
    if (commands.acresToBuy > 0) {
        return game.wheat - static_cast<double>(commands.acresToBuy) * game.landPrice;
    }

    if (commands.acresToSell > 0) {
        return game.wheat + static_cast<double>(commands.acresToSell) * game.landPrice;
    }

    return game.wheat;
}

// Выводит сообщение об ошибке команды игрока
static void PrintCommandError(const GameParameters& game, const UserCommands& commands, CommandError error) {

    std::cout << "\nО, повелитель, пощади нас! У нас только " << game.population << " человек, "
        << FormatNumber(game.wheat) << " бушелей пшеницы и " << game.acres << " акров земли!\n";

    switch (error) {
    case CommandError::None:
        break;

    case CommandError::Negative:
        std::cout << "Значения не могут быть отрицательными.\n";
        break;

    case CommandError::BuyAndSell:
        std::cout << "Нельзя одновременно покупать и продавать землю.\n";
        break;

    case CommandError::NotEnoughWheatForLand:
        std::cout << "Покупка " << commands.acresToBuy << " акров обойдётся в "
            << FormatNumber(static_cast<double>(commands.acresToBuy) * game.landPrice)
            << " бушелей, а в амбарах только " << FormatNumber(game.wheat) << ".\n";
        break;

    case CommandError::NotEnoughLand:
        std::cout << "Нельзя продать " << commands.acresToSell << " акров: у города их только "
            << game.acres << ".\n";
        break;

    case CommandError::NotEnoughWheatForSeeds:
        std::cout << "Нельзя выделить на посев " << FormatNumber(commands.seedsToSow)
            << " бушелей: после торговли в амбарах будет только "
            << FormatNumber(WheatAfterTrade(game, commands)) << ".\n";
        break;
    }

    std::cout << '\n';
}

// Выводит сообщение о поражении
static void PrintDefeat(const GameParameters& game, GameStatus status, const GameConfig& cfg) {
    std::cout << '\n';

    if (status == GameStatus::LostStarvation) {
        std::cout << "О, повелитель! В этом году от голода умерло " << game.starved << " человек из "
            << game.population << " - это больше " << FormatNumber(cfg.starvationLimit * 100.0)
            << "% населения.\n"
            << "Разъярённый народ низверг вас с трона. Игра окончена.\n";
    }
    else {
        std::cout << "Чума унесла жизни последних жителей. Город опустел, и править больше некем.\n"
            << "Игра окончена.\n";
    }
}

// Создает начальное состояние игры
GameParameters CreateNewGame(const GameConfig& cfg) {
    GameParameters game;
    game.round = 1;
    game.population = cfg.startPopulation;
    game.wheat = Round2(cfg.startWheat);
    game.acres = cfg.startAcres;
    game.landPrice = RollLandPrice(cfg);

    return game;
}

// Проверяет команды игрока на соответствие правилам игры
CommandError ValidateCommands(const GameParameters& game, const UserCommands& commands) {
    if (commands.acresToBuy < 0 || commands.acresToSell < 0 ||
        !std::isfinite(commands.seedsToSow) || commands.seedsToSow < 0.0) {
        return CommandError::Negative;
    }

    if (commands.acresToBuy > 0 && commands.acresToSell > 0) {
        return CommandError::BuyAndSell;
    }

    if (commands.acresToBuy > 0 &&
        static_cast<double>(commands.acresToBuy) * game.landPrice > game.wheat + kEpsilon) {
        return CommandError::NotEnoughWheatForLand;
    }

    if (commands.acresToSell > game.acres) {
        return CommandError::NotEnoughLand;
    }

    if (Round2(commands.seedsToSow) > WheatAfterTrade(game, commands) + kEpsilon) {
        return CommandError::NotEnoughWheatForSeeds;
    }

    return CommandError::None;
}

// Выполняет один игровой раунд
GameStatus RunOneRound(GameParameters& game, const UserCommands& commands, const GameConfig& cfg) {
    if (game.population <= 0) {
        return GameStatus::LostAllDead;
    }

    // Некорректная команда считается пропуском хода.
    const UserCommands cmd = ValidateCommands(game, commands) == CommandError::None ? commands : UserCommands{};

    const int populationAtStart = game.population;

    // Торговля землей
    if (cmd.acresToBuy > 0) {
        game.acres += cmd.acresToBuy;
        game.wheat = Round2(game.wheat - static_cast<double>(cmd.acresToBuy) * game.landPrice);
    }
    else if (cmd.acresToSell > 0) {
        game.acres -= cmd.acresToSell;
        game.wheat = Round2(game.wheat + static_cast<double>(cmd.acresToSell) * game.landPrice);
    }

    // Посев
    const double seeds = Round2(cmd.seedsToSow);
    game.wheat = Round2(game.wheat - seeds);

    const double coveredAcres = std::floor(seeds / cfg.seedPerAcre + kEpsilon);
    const double workableAcres = std::floor(cfg.acresPerCitizen * game.population);
    const double sownAcres = std::min({coveredAcres, static_cast<double>(game.acres), workableAcres});

    // Сбор урожая
    game.yieldPerAcre = RandomInt(cfg.yieldMin, cfg.yieldMax);
    game.harvest = Round2(sownAcres * game.yieldPerAcre);
    game.wheat = Round2(game.wheat + game.harvest);

    // Крысы
    game.ratsEaten = Round2(game.wheat * RandomReal(0.0, cfg.ratsMaxShare));
    game.wheat = Round2(game.wheat - game.ratsEaten);

    // Население потребляет пшеницу
    const double fedCitizens = std::floor((game.wheat + kEpsilon) / cfg.wheatPerCitizen);
    game.starved = populationAtStart - static_cast<int>(std::min<double>(fedCitizens, populationAtStart));

    game.wheat = Round2(std::max(0.0, game.wheat - populationAtStart * cfg.wheatPerCitizen));

    // Проверка на смертность
    if (game.starved > cfg.starvationLimit * populationAtStart + kEpsilon) {
        return GameStatus::LostStarvation;
    }

    // Прирост населения
    const double arrivals = game.starved / kArrivalsStarvedDivisor +
        (kArrivalsBaseYield - game.yieldPerAcre) * game.wheat / kArrivalsWheatDivisor +
        kArrivalsBonus;

    game.arrived = static_cast<int>(std::clamp(std::floor(arrivals), 0.0, static_cast<double>(cfg.maxArrivals)));

    game.population = populationAtStart - game.starved + game.arrived;

    // Чума
    game.plague = RandomReal(0.0, 1.0) < cfg.plagueChance;

    if (game.plague) {
        game.population /= 2;
    }

    if (game.population <= 0) {
        game.population = 0;
        return GameStatus::LostAllDead;
    }

    // Подсчет статистики
    game.sumStarvationPercent += 100.0 * game.starved / populationAtStart;
    game.sumAcresPerCitizen += static_cast<double>(game.acres) / game.population;

    // Переходим к следующему раунду
    ++game.round;

    if (game.round > cfg.totalRounds) {
        return GameStatus::Completed;
    }

    game.landPrice = RollLandPrice(cfg);
    return GameStatus::Running;
}

// Получает команды игрока и проверяет их корректность
std::optional<UserCommands> GetUserCommands(const GameParameters& game) {
    while (true) {
        UserCommands commands;

        if (!PromptInt("Сколько акров земли повелеваешь купить? ", commands.acresToBuy)) {
            return std::nullopt;
        }

        if (commands.acresToBuy == 0 && !PromptInt("Сколько акров земли повелеваешь продать? ", commands.acresToSell)) {
            return std::nullopt;
        }

        if (!PromptDouble("Сколько бушелей пшеницы повелеваешь засеять? ", commands.seedsToSow)) {
            return std::nullopt;
        }

        const CommandError error = ValidateCommands(game, commands);

        if (error == CommandError::None) {
            return commands;
        }

        PrintCommandError(game, commands, error);
    }
}

// Выводит отчет о текущем раунде
void PrintRoundReport(const GameParameters& game) {
    std::cout << '\n';

    if (game.round == 1) {
        std::cout << "Рад видеть вас на троне, милорд! Пора приступать к правлению!\n"
            << "в году 1 вашего высочайшего правления\n"
            << "Население города составляет " << game.population << " человек;\n"
            << "В амбарах лежит " << FormatNumber(game.wheat) << " бушелей пшеницы;\n"
            << "Город занимает " << game.acres << " акров;\n"
            << "1 акр земли стоит сейчас " << game.landPrice << " бушелей.\n\n";

        return;
    }

    std::cout << "Мой повелитель, соизволь поведать тебе\n"
        << "в году " << game.round << " твоего высочайшего правления\n";

    if (game.starved > 0 && game.arrived > 0) {
        std::cout << game.starved << " человек умерли с голоду, и " << game.arrived
            << " человек прибыли в наш великий город;\n";
    }
    else if (game.starved > 0) {
        std::cout << game.starved << " человек умерли с голоду;\n";
    }
    else if (game.arrived > 0) {
        std::cout << game.arrived << " человек прибыли в наш великий город;\n";
    }

    if (game.plague) {
        std::cout << "Чума уничтожила половину населения;\n";
    }

    std::cout << "Население города сейчас составляет " << game.population << " человек;\n"
        << "Мы собрали " << FormatNumber(game.harvest) << " бушелей пшеницы, по " << game.yieldPerAcre
        << " бушелей с акра;\n"
        << "Крысы истребили " << FormatNumber(game.ratsEaten) << " бушелей пшеницы, оставив "
        << FormatNumber(game.wheat) << " бушелей в амбарах;\n"
        << "Город сейчас занимает " << game.acres << " акров;\n"
        << "1 акр земли стоит сейчас " << game.landPrice << " бушелей.\n"
        << "Что пожелаешь, повелитель?\n\n";
}

// Выводит итоговую оценку правления
void PrintFinalResult(const GameParameters& game, const GameConfig& cfg) {
    const double rounds = static_cast<double>(cfg.totalRounds);

    const double starvationPercent = game.sumStarvationPercent / rounds; 
    const double acresPerCitizen = game.sumAcresPerCitizen / rounds;      

    const char* grade = nullptr;
    const char* verdict = nullptr;

    if (starvationPercent > cfg.gradeBadStarvationPercent ||
        acresPerCitizen < cfg.gradeBadAcresPerCitizen) {
        grade = "Плохо";
        verdict = "Из-за вашей некомпетентности в управлении, народ устроил бунт, и изгнал вас из города. "
            "Теперь вы вынуждены влачить жалкое существование в изгнании.";
    }
    else if (starvationPercent > cfg.gradeFairStarvationPercent ||
        acresPerCitizen < cfg.gradeFairAcresPerCitizen) {
        grade = "Удовлетворительно";
        verdict = "Вы правили железной рукой, подобно Нерону и Ивану Грозному. "
            "Народ вздохнул с облегчением, и никто больше не желает видеть вас правителем.";
    }
    else if (starvationPercent > cfg.gradeGoodStarvationPercent ||
        acresPerCitizen < cfg.gradeGoodAcresPerCitizen) {
        grade = "Хорошо";
        verdict = "Вы справились вполне неплохо, у вас, конечно, есть недоброжелатели, "
            "но многие хотели бы увидеть вас во главе города снова.";
    }
    else {
        grade = "Отлично";
        verdict = "Фантастика! Карл Великий, Дизраэли и Джефферсон вместе не справились бы лучше.";
    }

    std::cout << "\n===== Итоги правления =====\n"
        << "Среднегодовой процент умерших от голода: " << FormatNumber(starvationPercent) << "%\n"
        << "Акров земли на одного жителя: " << FormatNumber(acresPerCitizen) << "\n"
        << "Оценка: " << grade << "\n"
        << verdict << "\n";
}

// Спрашивает игрока, продолжать ли игру
bool AskContinueOrExit() {
    int answer = 0;

    while (true) {
        if (!PromptInt("Продолжить правление? (1 - продолжить, 0 - сохранить и выйти): ", answer)) {
            return false;
        }

        if (answer == 1) {
            return true;
        }
        if (answer == 0) {
            return false;
        }

        std::cout << "Пожалуйста, введите 1 или 0.\n";
    }
}

// Спрашивает игрока, продолжить ли сохраненную игру
bool AskResumeSavedGame(int savedRound) {
    std::cout << "Найдено сохранение: вы остановились на году " << savedRound << " правления.\n"
        << "  1 - продолжить прошлую игру\n"
        << "  2 - начать заново (старое сохранение будет перезаписано)\n";

    int answer = 0;

    while (true) {
        if (!PromptInt("Ваш выбор: ", answer)) {
            return true;
        }

        if (answer == 1) return true;
        if (answer == 2) return false;

        std::cout << "Пожалуйста, введите 1 или 2.\n";
    }
}

// Запускает игровой цикл
GameStatus RunGame(GameParameters& game, const GameConfig& cfg, const std::string& savePath) {

    GameStatus status = GameStatus::Running;

    while (status == GameStatus::Running) {
        const bool saved = SaveGame(savePath, game);

        if (!saved) {
            std::cerr << "Предупреждение: не удалось сохранить прогресс в файл " << savePath << ".\n";
        }

        std::cout << '\n';

        if (!AskContinueOrExit()) {
            std::cout << (saved ? "Прогресс сохранён. До встречи, повелитель!\n"
                : "До встречи, повелитель!\n");
            return GameStatus::PlayerExit;
        }

        PrintRoundReport(game);

        const std::optional<UserCommands> commands = GetUserCommands(game);

        if (!commands) {
            return GameStatus::PlayerExit;
        }

        status = RunOneRound(game, *commands, cfg);
    }

    if (status == GameStatus::Completed) {
        if (!SaveGame(savePath, game)) {
            std::cerr << "Предупреждение: не удалось сохранить итоговое состояние в файл " << savePath << ".\n";
        }

        PrintFinalResult(game, cfg);
    }
    else {
        PrintDefeat(game, status, cfg);

        if (!DeleteSave(savePath)) {
            std::cerr << "Предупреждение: не удалось удалить файл сохранения " << savePath
                << ". Удалите его вручную, иначе при следующем запуске игра предложит "
                << "продолжить проигранную партию.\n";
        }
    }

    return status;
}