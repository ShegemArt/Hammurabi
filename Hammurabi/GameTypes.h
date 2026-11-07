#pragma once

// Игровые константы
struct GameConfig {

    int startPopulation = 100;
    double startWheat = 2800.0;
    int startAcres = 1000;
    int totalRounds = 10;

    double wheatPerCitizen = 20.0;   
    double acresPerCitizen = 10.0;   
    double seedPerAcre = 0.5;   
    int priceMin = 17;    
    int priceMax = 26;
    int yieldMin = 1;      
    int yieldMax = 6;
    int maxArrivals = 50;     
    double ratsMaxShare = 0.07;   
    double plagueChance = 0.15;   
    double starvationLimit = 0.45;

    double gradeBadStarvationPercent = 33.0;  
    double gradeBadAcresPerCitizen = 7.0;

    double gradeFairStarvationPercent = 10.0;  
    double gradeFairAcresPerCitizen = 9.0; 

    double gradeGoodStarvationPercent = 3.0;   
    double gradeGoodAcresPerCitizen = 10.0;
};

// Параметры игры, отражающие состояние раунда игры
struct GameParameters {
    int round = 1;    
    int population = 0;
    double wheat = 0.0;   
    int acres = 0;
    int landPrice = 0;    

    int starved = 0;
    int arrived = 0;
    bool plague = false;
    double harvest = 0.0;
    int yieldPerAcre = 0;
    double ratsEaten = 0.0;

    double sumStarvationPercent = 0.0;  
    double sumAcresPerCitizen = 0.0;   
};

// Команды игрока на один раунд.
struct UserCommands {
    int acresToBuy = 0;
    int acresToSell = 0;
    double seedsToSow = 0.0;
};

// Состояние игры
enum class GameStatus {
    Running,          // Игра идет
    Completed,        // Игра доиграна до последнего раунда
    LostStarvation,   // Проигрыш по голоду
    LostAllDead,      // Проигрыш по смерти населения
    PlayerExit        // Выход из игры
};