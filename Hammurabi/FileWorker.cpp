#include "FileWorker.h"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

// Ограничения защищают сохранение от заведомо недопустимых значений.
static constexpr int kMaxCount = 100'000'000;
static constexpr double kMaxValue = 1e10;
static constexpr double kTolerance = 1e-3;

// Представляет одно поле JSON.
struct JsonEntry {
	std::string key;
	double value = 0.0;
	bool isBool = false;
};

// Разбирает плоский JSON, используемый в файле сохранения.
class FlatJsonParser {
public:
	explicit FlatJsonParser(std::string_view text) : text_(text) {}

	bool Parse(std::vector<JsonEntry>& entries) {
		SkipSpaces();

		if (!Consume('{')) return false;

		SkipSpaces();

		if (Consume('}')) return AtEnd();

		while (true) {
			JsonEntry entry;

			SkipSpaces();
			if (!ParseKey(entry.key)) {
				return false;
			}

			SkipSpaces();
			if (!Consume(':')) {
				return false;
			}

			SkipSpaces();
			if (!ParseValue(entry)) {
				return false;
			}

			// Повторяющиеся ключи не допускаются.
			for (const JsonEntry& existing : entries) {
				if (existing.key == entry.key) {
					return false;
				}
			}

			entries.push_back(std::move(entry));

			SkipSpaces();

			if (Consume(',')) {
				continue;
			}

			return Consume('}') && AtEnd();
		}
	}

private:

	std::string_view text_;
	std::size_t pos_ = 0;

	// Пропускает пробельные символы.
	void SkipSpaces() {
		while (pos_ < text_.size()) {
			const char c = text_[pos_];

			if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
				break;
			}

			++pos_;
		}
	}

	// Проверяет, что после JSON не осталось лишних символов.
	bool AtEnd() {
		SkipSpaces();
		return pos_ == text_.size();
	}

	// Проверяет следующий символ и перемещает позицию чтения.
	bool Consume(char expected) {
		if (pos_ >= text_.size() || text_[pos_] != expected) {
			return false;
		}

		++pos_;
		return true;
	}

	// Считывает название поля.
	bool ParseKey(std::string& key) {
		if (!Consume('"')) {
			return false;
		}

		const std::size_t start = pos_;

		while (pos_ < text_.size()) {
			const unsigned char c = static_cast<unsigned char>(text_[pos_]);

			if (c == '"') {
				key.assign(text_.substr(start, pos_ - start));
				++pos_;
				return true;
			}

			// Ключи сохранения ASCII; escape-последовательности им не нужны.
			if (c == '\\' || c < 0x20) return false;

			++pos_;
		}

		return false;
	}

	// Считывает логическое или числовое значение.
	bool ParseValue(JsonEntry& entry) {
		if (text_.substr(pos_, 4) == "true") {
			pos_ += 4;
			entry.value = 1.0;
			entry.isBool = true;
			return true;
		}

		if (text_.substr(pos_, 5) == "false") {
			pos_ += 5;
			entry.value = 0.0;
			entry.isBool = true;
			return true;
		}

		// Проверяем грамматику JSON-числа до преобразования в double.
		const std::size_t start = pos_;

		if (pos_ < text_.size() && text_[pos_] == '-') {
			++pos_;
		}

		if (pos_ >= text_.size()) {
			return false;
		}

		if (text_[pos_] == '0') {
			++pos_;

			if (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
				return false;
			}
		}
		else if (text_[pos_] >= '1' && text_[pos_] <= '9') {
			while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
				++pos_;
			}
		}
		else {
			return false;
		}

		if (pos_ < text_.size() && text_[pos_] == '.') {
			++pos_;

			const std::size_t fractionStart = pos_;

			while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
				++pos_;
			}

			if (pos_ == fractionStart) {
				return false;
			}
		}

		if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
			++pos_;

			if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) {
				++pos_;
			}

			const std::size_t exponentStart = pos_;

			while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
				++pos_;
			}

			if (pos_ == exponentStart) {
				return false;
			}
		}

		const std::string token(text_.substr(start, pos_ - start));

		char* end = nullptr;
		errno = 0;

		const double value = std::strtod(token.c_str(), &end);

		if (end != token.c_str() + token.size() || errno == ERANGE || !std::isfinite(value)) {
			return false;
		}

		entry.value = value;
		entry.isBool = false;
		return true;
	}
};

// Находит поле JSON по названию.
static const JsonEntry* FindEntry(const std::vector<JsonEntry>& entries, std::string_view key) {

	for (const JsonEntry& entry : entries) {
		if (entry.key == key) {
			return &entry;
		}
	}

	return nullptr;
}

// Считывает целочисленное значение.
static bool ReadInt(const std::vector<JsonEntry>& entries, std::string_view key, int& out) {

	const JsonEntry* entry = FindEntry(entries, key);

	if (entry == nullptr || entry->isBool || !std::isfinite(entry->value)) {
		return false;
	}

	if (std::floor(entry->value) != entry->value ||
		entry->value < static_cast<double>(std::numeric_limits<int>::min()) ||
		entry->value > static_cast<double>(std::numeric_limits<int>::max())) {
		return false;
	}

	out = static_cast<int>(entry->value);
	return true;
}

// Считывает вещественное значение.
static bool ReadDouble(const std::vector<JsonEntry>& entries, std::string_view key, double& out) {

	const JsonEntry* entry = FindEntry(entries, key);

	if (entry == nullptr || entry->isBool || !std::isfinite(entry->value)) {
		return false;
	}

	out = entry->value;
	return true;
}

// Считывает логическое значение.
static bool ReadBool(const std::vector<JsonEntry>& entries, std::string_view key, bool& out) {

	const JsonEntry* entry = FindEntry(entries, key);

	if (entry == nullptr || !entry->isBool) {
		return false;
	}

	out = entry->value != 0.0;
	return true;
}

// Проверяет, находится ли число в заданном диапазоне.
static bool IsFiniteInRange(double value, double minimum, double maximum) {
	return std::isfinite(value) && value >= minimum && value <= maximum;
}

// Проверяет, что значение имеет точность не более двух знаков после запятой.
static bool HasTwoDecimalPlaces(double value) {
	const double cents = value * 100.0;
	return std::fabs(cents - std::round(cents)) < kTolerance;
}

// Проверяет основные диапазоны значений игрового состояния.
static bool HasBasicValueRanges(const GameParameters& params) {
	if (params.round < 1 || params.round > kMaxCount) {
		return false;
	}
	if (params.population < 1 || params.population > kMaxCount) {
		return false;
	}
	if (params.acres < 0 || params.acres > kMaxCount) {
		return false;
	}
	if (params.landPrice < 1 || params.landPrice > kMaxCount) {
		return false;
	}
	if (params.starved < 0 || params.starved > kMaxCount) {
		return false;
	}
	if (params.arrived < 0 || params.arrived > kMaxCount) {
		return false;
	}
	if (params.yieldPerAcre < 0 || params.yieldPerAcre > kMaxCount) {
		return false;
	}

	if (!IsFiniteInRange(params.wheat, 0.0, kMaxValue) || !IsFiniteInRange(params.harvest, 0.0, kMaxValue) ||
		!IsFiniteInRange(params.ratsEaten, 0.0, kMaxValue) || !IsFiniteInRange(params.sumStarvationPercent, 0.0, kMaxValue) ||
		!IsFiniteInRange(params.sumAcresPerCitizen, 0.0, kMaxValue)) {
		return false;
	}

	return HasTwoDecimalPlaces(params.wheat) && HasTwoDecimalPlaces(params.harvest) && HasTwoDecimalPlaces(params.ratsEaten);
}

// Преобразует JSON в состояние игры.
static std::optional<GameParameters> ParseJson(std::string_view text) {
	std::vector<JsonEntry> entries;

	if (!FlatJsonParser(text).Parse(entries)) {
		return std::nullopt;
	}

	GameParameters params;

	const bool fieldsValid = ReadInt(entries, "round", params.round) &&
		ReadInt(entries, "population", params.population) &&
		ReadDouble(entries, "wheat", params.wheat) &&
		ReadInt(entries, "acres", params.acres) &&
		ReadInt(entries, "landPrice", params.landPrice) &&
		ReadInt(entries, "starved", params.starved) &&
		ReadInt(entries, "arrived", params.arrived) &&
		ReadBool(entries, "plague", params.plague) &&
		ReadDouble(entries, "harvest", params.harvest) &&
		ReadInt(entries, "yieldPerAcre", params.yieldPerAcre) &&
		ReadDouble(entries, "ratsEaten", params.ratsEaten) &&
		ReadDouble(entries, "sumStarvationPercent", params.sumStarvationPercent) &&
		ReadDouble(entries, "sumAcresPerCitizen", params.sumAcresPerCitizen);

	if (!fieldsValid || !HasBasicValueRanges(params)) {
		return std::nullopt;
	}

	return params;
}

// Форматирует вещественное число с сохранением точности.
static std::string FormatDouble(double value) {
	char buffer[64];

	for (int precision = 15; precision <= 17; ++precision) {
		std::snprintf(buffer, sizeof(buffer), "%.*g", precision, value);

		if (std::strtod(buffer, nullptr) == value) {
			return buffer;
		}
	}

	return buffer;
}

// Преобразует состояние игры в JSON.
static std::string SerializeJson(const GameParameters& params) {
	std::ostringstream out;

	out << "{\n"
		<< "  \"round\": " << params.round << ",\n"
		<< "  \"population\": " << params.population << ",\n"
		<< "  \"wheat\": " << FormatDouble(params.wheat) << ",\n"
		<< "  \"acres\": " << params.acres << ",\n"
		<< "  \"landPrice\": " << params.landPrice << ",\n"
		<< "  \"starved\": " << params.starved << ",\n"
		<< "  \"arrived\": " << params.arrived << ",\n"
		<< "  \"plague\": " << (params.plague ? "true" : "false") << ",\n"
		<< "  \"harvest\": " << FormatDouble(params.harvest) << ",\n"
		<< "  \"yieldPerAcre\": " << params.yieldPerAcre << ",\n"
		<< "  \"ratsEaten\": " << FormatDouble(params.ratsEaten) << ",\n"
		<< "  \"sumStarvationPercent\": " << FormatDouble(params.sumStarvationPercent) << ",\n"
		<< "  \"sumAcresPerCitizen\": " << FormatDouble(params.sumAcresPerCitizen) << "\n"
		<< "}\n";

	return out.str();
}

// Проверяет ограничения, зависящие от настроек игры.
static bool IsStateValid(const GameParameters& params, const GameConfig& cfg, int maxRound) {

	if (!HasBasicValueRanges(params)) {
		return false;
	}
	if (params.round > maxRound) {
		return false;
	}
	if (params.landPrice < cfg.priceMin || params.landPrice > cfg.priceMax) {
		return false;
	}
	if (params.arrived > cfg.maxArrivals) {
		return false;
	}

	if (params.round == 1) {
		return params.starved == 0 && params.arrived == 0 && !params.plague &&
			params.yieldPerAcre == 0 && params.harvest == 0.0 && params.ratsEaten == 0.0 &&
			params.sumStarvationPercent == 0.0 && params.sumAcresPerCitizen == 0.0;
	}

	return params.yieldPerAcre >= cfg.yieldMin && params.yieldPerAcre <= cfg.yieldMax;
}

// Считывает содержимое файла в строку.
static bool ReadTextFile(const std::string& path, std::string& text) {
	std::ifstream input(path, std::ios::binary);

	if (!input) {
		return false;
	}

	text.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());

	return !input.bad();
}

// Записывает текст во временный файл и затем заменяет им основное сохранение.
static bool WriteTextFile(const std::string& path, std::string_view text) {
	const std::string temporaryPath = path + ".tmp";

	std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);

	if (!output) {
		return false;
	}

	output.write(text.data(), static_cast<std::streamsize>(text.size()));
	output.close();

	if (!output) {
		std::error_code ignored;
		std::filesystem::remove(temporaryPath, ignored);
		return false;
	}

	std::error_code error;
	std::filesystem::rename(temporaryPath, path, error);

	if (error) {
		std::error_code ignored;
		std::filesystem::remove(temporaryPath, ignored);
		return false;
	}

	return true;
}

// Конвертирует состояние игры в JSON.
std::string ToJson(const GameParameters& params) {
	return SerializeJson(params);
}

// Конвертирует JSON в состояние игры.
std::optional<GameParameters> FromJson(std::string_view text) {
	return ParseJson(text);
}

// Проверяет корректность сохранения.
bool IsSaveValid(const GameParameters& params, const GameConfig& cfg) {
	return IsStateValid(params, cfg, cfg.totalRounds);
}

// Загружает сохранение.
LoadResult LoadGame(const std::string& path, const GameConfig& cfg) {
	std::error_code error;

	const bool exists = std::filesystem::exists(path, error);

	if (error) {
		return { LoadStatus::Corrupted, std::nullopt };
	}

	if (!exists) {
		return { LoadStatus::NotFound, std::nullopt };
	}

	const bool regularFile = std::filesystem::is_regular_file(path, error);

	if (error || !regularFile) {
		return { LoadStatus::Corrupted, std::nullopt };
	}

	std::string text;

	if (!ReadTextFile(path, text)) {
		return { LoadStatus::Corrupted, std::nullopt };
	}

	const std::optional<GameParameters> params = ParseJson(text);

	if (!params) {
		return { LoadStatus::Corrupted, std::nullopt };
	}

	if (IsStateValid(*params, cfg, cfg.totalRounds)) {
		return { LoadStatus::Loaded, params };
	}

	if (params->round == cfg.totalRounds + 1 && IsStateValid(*params, cfg, cfg.totalRounds + 1)) {
		return { LoadStatus::Finished, std::nullopt };
	}

	return { LoadStatus::Corrupted, std::nullopt };
}

// Записывает сохранение.
bool SaveGame(const std::string& path, const GameParameters& params) {
	return WriteTextFile(path, SerializeJson(params));
}

// Удаляет сохранение и временный файл.
bool DeleteSave(const std::string& path) {
	std::error_code error;

	std::filesystem::remove(path, error);

	if (error) {
		return false;
	}

	std::error_code ignored;
	std::filesystem::remove(path + ".tmp", ignored);

	return true;
}