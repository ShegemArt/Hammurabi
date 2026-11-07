#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "GameTypes.h"

// Конвертация сохранения в JSON
std::string ToJson(const GameParameters& params);

// Перевод JSON сохранение в текущие параметры игры: nullopt - если файл сломан или данные не корректные, иначе - парметры игры
std::optional<GameParameters> FromJson(std::string_view text);

// Состояние файла
enum class LoadStatus {
    Loaded,      // Сохранение найдено и загружено
    NotFound,    // Сохранения нет
    Finished,    // Завершенная игра
    Corrupted    // Ошибка преобразования
};

// Результат загрузки сохранения
struct LoadResult {
    LoadStatus status = LoadStatus::NotFound;
    std::optional<GameParameters> game;
};

// Загружает сохранение
LoadResult LoadGame(const std::string& path, const GameConfig& cfg);

// Запись сохранения в файл, при ошибке - false, иначе - true
bool SaveGame(const std::string& path, const GameParameters& params);

// Удаляет сохранение true - файла больше нет, иначе - ошибка удаления.
bool DeleteSave(const std::string& path);

// Проверка сохранения
bool IsSaveValid(const GameParameters& params, const GameConfig& cfg);