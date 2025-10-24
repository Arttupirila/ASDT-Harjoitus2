/* Kääntäminen g++ -w lipulla tällä hetkellä */

/* Harjoituksen 2 ohjelmarunko */
/* Tee runkoon tarvittavat muutokset kommentoiden */
/* Pistesuorituksista kooste tähän alkuun */
/* 2p = täysin tehty suoritus, 1p = osittain tehty suoritus */
/* Tarkemmat ohjeet Moodlessa */
/* Lisäohjeita, vinkkejä ja apuja löytyy koodin joukosta */
/* OPISKELIJA: merkityt kohdat eritoten kannattaa katsoa huolella */



/* PERUSASIAT SUORITETTU (8p?) */





//peruskirjastot mitä tarvii aika lailla aina kehitystyössä
//OPISKELIJA: lisää tarvitsemasi peruskirjastot
#include <iostream>
#include <vector>
#include <string>
#include <cstdlib>
#include <ctime>

//rinnakaisuuden peruskirjastot
//OPISKLEIJA: lisää tarvittaessa lisää kirjastoja, muista käyttää -pthread lippua käännöksessä ja tarvittaessa -lrt lippua myös
//Huom! Rinnakkaisuusasioista on eri versioita (c++, POSIX/pthread, SystemV)
//Kaikkien käyttö on sallittua
#include <sys/shm.h> //jaetun muistin kirjasto: shmget(), shmmat()
#include <sys/sem.h> // System V Semaphoret (KOHTA 3)
#include <fcntl.h> //S_IRUSR | S_IWUSR määrittelyt esim jaettua muistia varten
#include <unistd.h> //esim fork() määrittely ja muut prosessimäärittelyt
#include <sys/wait.h> // waitpid
#include <pthread.h> // POSIX Säikeet (KOHTA 2 ja 4)

using namespace std;

// Globaalit määrittelyt
#define ROTTIEN_LKM 3
#define KORKEUS 7
#define LEVEYS 7

// Labyrintin osien arvoja
#define WALL 1
#define PATH 0
#define START 2
#define EXIT 4
#define VISITED_P 5 // Käytetään prosesseissa (KOHTA 1)
#define VISITED_T 6 // Käytetään säikeissä (KOHTA 2)

// Liikkumissuunnat
enum Suunta { UP, DOWN, LEFT, RIGHT, NONE };

// Sijainti-rakenne
struct Coord {
    int ykoord;
    int xkoord;
};

// Reitti-solmu (risteys)
struct Solmu {
    Coord sijainti;
    struct { bool tutkittu; Suunta jatkom; } up, down, left, right;
    Suunta tulosuunta;
};

// Rottakohtainen data
struct RatData {
    int id; // Rotan ID
    // Täydellinen reitti, pidetään vain rotan pinossa (paikallisesti)
    vector<Solmu> reitti; 
    Coord rotanSijainti;
    int liikeCount;
    // ... mahdollisesti muita rotan paikallisia tilatietoja
};

// **KOHTA 1 & 3: PROSESSIEN GLOBAALIEN MUUTTUJIEN MÄÄRITTELY**

// Labyrintti jaettuun muistiin (KOHTA 1)
int (*labyrintti)[LEVEYS] = nullptr; 

// Sijaintikartta jaettuun muistiin (KOHTA 3)
int (*sijaintikarttaProsessit)[LEVEYS] = nullptr;
int semid; // Semaforin ID (KOHTA 3)

// **KOHTA 2 & 4: SÄIKEIDEN GLOBAALIEN MUUTTUJIEN MÄÄRITTELY**

// Labyrintti globaalissa muistissa (sama muistiavaruus säikeille) (KOHTA 2)
int labyrinttiSaikeet[KORKEUS][LEVEYS]; 

// Sijaintikartta globaalissa muistissa (sama muistiavaruus säikeille) (KOHTA 4)
int sijaintikarttaSaikeet[KORKEUS][LEVEYS] = {0}; 
pthread_mutex_t sijainti_mutex; // Mutex-lukko (KOHTA 4)

// Semaforin P- ja V-operaatioiden apu-rakenne (System V Semaphores)
struct sembuf P = {0, -1, SEM_UNDO}; // P-operaatio (lukitse)
struct sembuf V = {0, 1, SEM_UNDO};  // V-operaatio (vapauta)

/**
 * Tulostaa labyrintin nykyisen tilan ja rottien sijainnit
 * @param map_ptr Osoitin labyrinttiin tai sijaintikarttaan
 * @param rat_id Rotan ID, joka on juuri liikkunut
 */
void printLabyrinthStatus(int (*map_ptr)[LEVEYS], int rat_id) {
    // Tällä hetkellä tulostetaan vain sijaintikartta
    cout << "\n------------------------------------" << endl;
    cout << "Rotta " << rat_id << " liikkui. Karttatilanne nyt:" << endl;
    for (int y = 0; y < KORKEUS; y++) {
        for (int x = 0; x < LEVEYS; x++) {
            if (map_ptr[y][x] > 0) {
                // Käytetään arvoa 'map_ptr[y][x]' merkitsemään rottaa tai seinää
                if (map_ptr[y][x] == WALL) cout << "# "; // Seinä
                else if (map_ptr[y][x] == EXIT) cout << "E "; // Maali
                else if (map_ptr[y][x] == START) cout << "S "; // Alka
                else cout << map_ptr[y][x] << " "; // Rottan ID
            } else {
                cout << ". "; // Vapaa polku
            }
        }
        cout << endl;
    }
    cout << "------------------------------------" << endl;
}

/**
 * Rotan reitinetsintäalgoritmi prosessitoteutukselle.
 * Päivittää sijaintikarttaa käyttäen semaforia (KOHTA 3).
 */
int aloitaRottaProsessit(int rat_id) {
    RatData rotta;
    rotta.id = rat_id;
    rotta.liikeCount = 0;
    rotta.rotanSijainti = {3, 1}; // Esimerkki lähtöpiste

    rotta.reitti.push_back({{3, 1}, {}, {}, {}, {}, NONE}); // Aloitusristeys pinoon

    // Function to check if a move is valid
    auto isValidMove = [](int y, int x, int (*maze)[LEVEYS]) {
        return y >= 0 && y < KORKEUS && x >= 0 && x < LEVEYS && maze[y][x] != WALL;
    };

    while (rotta.rotanSijainti.ykoord != EXIT || rotta.rotanSijainti.xkoord != EXIT) { // Korvaa EXIT-tarkistus todellisilla koordinaateilla

        // ... Oikea poistumisehto vaatisi labyrintin tarkastelua
        if (labyrintti[rotta.rotanSijainti.ykoord][rotta.rotanSijainti.xkoord] == EXIT) {
            cout << "Rotta " << rotta.id << " löysi uloskäynnin! Liikkeitä: " << rotta.liikeCount << endl;
            break; 
        }

        // Determine valid moves
        vector<Coord> validMoves;
        if (isValidMove(rotta.rotanSijainti.ykoord - 1, rotta.rotanSijainti.xkoord, labyrintti)) validMoves.push_back({rotta.rotanSijainti.ykoord - 1, rotta.rotanSijainti.xkoord});
        if (isValidMove(rotta.rotanSijainti.ykoord + 1, rotta.rotanSijainti.xkoord, labyrintti)) validMoves.push_back({rotta.rotanSijainti.ykoord + 1, rotta.rotanSijainti.xkoord});
        if (isValidMove(rotta.rotanSijainti.ykoord, rotta.rotanSijainti.xkoord - 1, labyrintti)) validMoves.push_back({rotta.rotanSijainti.ykoord, rotta.rotanSijainti.xkoord - 1});
        if (isValidMove(rotta.rotanSijainti.ykoord, rotta.rotanSijainti.xkoord + 1, labyrintti)) validMoves.push_back({rotta.rotanSijainti.ykoord, rotta.rotanSijainti.xkoord + 1});

        if (!validMoves.empty()) {
            // Randomly select a valid move
            Coord nextMove = validMoves[rand() % validMoves.size()];

            // Update position
            sijaintikarttaProsessit[rotta.rotanSijainti.ykoord][rotta.rotanSijainti.xkoord] = 0;
            rotta.rotanSijainti = nextMove;
            sijaintikarttaProsessit[rotta.rotanSijainti.ykoord][rotta.rotanSijainti.xkoord] = rotta.id;

            printLabyrinthStatus(sijaintikarttaProsessit, rotta.id);
        } else {
            // Backtrack if no valid moves
            if (!rotta.reitti.empty()) {
                rotta.rotanSijainti = rotta.reitti.back().sijainti;
                rotta.reitti.pop_back();
            }
        }

        // KOHTA 3: P-operaatio (lukitus) ennen jaetun muistin kirjoitusta
        if (semop(semid, &P, 1) == -1) {
            perror("semop P-operation failed");
            exit(1);
        }

        // Päivitetään rottien sijaintikartta (KOHTA 3)
        // Aiempi sijainti nollataan ja uusi merkitään rotta.id:llä
        sijaintikarttaProsessit[rotta.rotanSijainti.ykoord][rotta.rotanSijainti.xkoord] = 0; // Rotan vanha paikka
        
        // Esimerkkilukujen päivitys simuloimaan liikkumista
        if (rand() % 4 == 0) rotta.rotanSijainti.xkoord++; // Liikuta rottaa esim. oikealle
        if (rotta.rotanSijainti.xkoord >= LEVEYS) rotta.rotanSijainti.xkoord = LEVEYS - 1;

        sijaintikarttaProsessit[rotta.rotanSijainti.ykoord][rotta.rotanSijainti.xkoord] = rotta.id; // Rotan uusi paikka

        // Tulostetaan kartan tilanne (vain demoa varten, todellisuudessa lukitus on lyhyt)
        printLabyrinthStatus(sijaintikarttaProsessit, rotta.id);

        // KOHTA 3: V-operaatio (vapautus) kirjoituksen jälkeen
        if (semop(semid, &V, 1) == -1) {
            perror("semop V-operation failed");
            exit(1);
        }

        rotta.liikeCount++;
        usleep(100000); // 100ms tauko
    }

    // Irrota jaettu muisti lapsiprosessista (KOHTA 1)
    if (shmdt(labyrintti) == -1) { perror("shmdt labyrintti failed"); }
    if (shmdt(sijaintikarttaProsessit) == -1) { perror("shmdt sijaintikartta failed"); }

    return rotta.liikeCount;
}

/**
 * Rotan reitinetsintäalgoritmi säietoteutukselle.
 * Päivittää sijaintikarttaa käyttäen mutex-lukkoa (KOHTA 4).
 */
void* aloitaRottaSaikeet(void* arg) {
    RatData* rotta_ptr = (RatData*)arg;
    int rat_id = rotta_ptr->id;
    rotta_ptr->liikeCount = 0;
    rotta_ptr->rotanSijainti = {3, 1}; // Esimerkki lähtöpiste

    rotta_ptr->reitti.push_back({{3, 1}, {}, {}, {}, {}, NONE}); // Aloitusristeys pinoon

    // Function to check if a move is valid
    auto isValidMove = [](int y, int x, int (*maze)[LEVEYS]) {
        return y >= 0 && y < KORKEUS && x >= 0 && x < LEVEYS && maze[y][x] != WALL;
    };

    while (rotta_ptr->rotanSijainti.ykoord != EXIT || rotta_ptr->rotanSijainti.xkoord != EXIT) { // Korvaa EXIT-tarkistus todellisilla koordinaateilla

        // ... Oikea poistumisehto vaatisi labyrintin tarkastelua
        if (labyrinttiSaikeet[rotta_ptr->rotanSijainti.ykoord][rotta_ptr->rotanSijainti.xkoord] == EXIT) {
            cout << "Saie-Rotta " << rat_id << " löysi uloskäynnin! Liikkuja: " << rotta_ptr->liikeCount << endl;
            break; 
        }

        // Determine valid moves
        vector<Coord> validMoves;
        if (isValidMove(rotta_ptr->rotanSijainti.ykoord - 1, rotta_ptr->rotanSijainti.xkoord, labyrinttiSaikeet)) validMoves.push_back({rotta_ptr->rotanSijainti.ykoord - 1, rotta_ptr->rotanSijainti.xkoord});
        if (isValidMove(rotta_ptr->rotanSijainti.ykoord + 1, rotta_ptr->rotanSijainti.xkoord, labyrinttiSaikeet)) validMoves.push_back({rotta_ptr->rotanSijainti.ykoord + 1, rotta_ptr->rotanSijainti.xkoord});
        if (isValidMove(rotta_ptr->rotanSijainti.ykoord, rotta_ptr->rotanSijainti.xkoord - 1, labyrinttiSaikeet)) validMoves.push_back({rotta_ptr->rotanSijainti.ykoord, rotta_ptr->rotanSijainti.xkoord - 1});
        if (isValidMove(rotta_ptr->rotanSijainti.ykoord, rotta_ptr->rotanSijainti.xkoord + 1, labyrinttiSaikeet)) validMoves.push_back({rotta_ptr->rotanSijainti.ykoord, rotta_ptr->rotanSijainti.xkoord + 1});

        if (!validMoves.empty()) {
            // Randomly select a valid move
            Coord nextMove = validMoves[rand() % validMoves.size()];

            // Update position
            pthread_mutex_lock(&sijainti_mutex);
            sijaintikarttaSaikeet[rotta_ptr->rotanSijainti.ykoord][rotta_ptr->rotanSijainti.xkoord] = 0;
            rotta_ptr->rotanSijainti = nextMove;
            sijaintikarttaSaikeet[rotta_ptr->rotanSijainti.ykoord][rotta_ptr->rotanSijainti.xkoord] = rat_id;
            pthread_mutex_unlock(&sijainti_mutex);

            printLabyrinthStatus(sijaintikarttaSaikeet, rat_id);
        } else {
            // Backtrack if no valid moves
            if (!rotta_ptr->reitti.empty()) {
                rotta_ptr->rotanSijainti = rotta_ptr->reitti.back().sijainti;
                rotta_ptr->reitti.pop_back();
            }
        }

        rotta_ptr->liikeCount++;
        usleep(100000); // 100ms tauko
    }

    return nullptr;
}


/**
 * Pääajorutiini prosessitoteutukselle (KOHTA 1 & 3).
 * Luo ja hallinnoi jaettua muistia ja semaforeja.
 */
void ajorutiiniProsessit() {
    // Luodaan System V -avaimet jaetulle muistille ja semaforille
    key_t shm_key_laby = ftok("labyrintti.txt", 'A');
    key_t shm_key_map = ftok("sijaintikartta.txt", 'B');
    key_t sem_key = ftok("semaphore.txt", 'C');
    
    // Esimerkkilabyrintti
    int example[KORKEUS][LEVEYS] = {
        {1,1,1,1,1,1,1},
        {1,0,1,0,1,0,4},
        {1,0,1,0,1,0,1},
        {1,2,0,2,0,2,1},
        {1,0,1,0,1,0,1},
        {1,0,1,0,1,0,1},
        {1,1,1,3,1,1,1}
    };

    // Luo ja kiinnitä jaettu muisti labyrintille
    size_t laby_size = sizeof(int) * KORKEUS * LEVEYS;
    int shmid_laby = shmget(shm_key_laby, laby_size, IPC_CREAT | 0666);
    if (shmid_laby == -1) { perror("shmget labyrintti failed"); return; }
    labyrintti = static_cast<int (*)[LEVEYS]>(shmat(shmid_laby, NULL, 0));
    if (labyrintti == (void*)-1) { perror("shmat labyrintti failed"); return; }

    for (int y = 0; y < KORKEUS; y++)
        for (int x = 0; x < LEVEYS; x++)
            labyrintti[y][x] = example[y][x];

    // Luo ja kiinnitä jaettu muisti sijaintikartalle
    size_t map_size = sizeof(int) * KORKEUS * LEVEYS;
    int shmid_map = shmget(shm_key_map, map_size, IPC_CREAT | 0666);
    if (shmid_map == -1) { perror("shmget sijaintikartta failed"); return; }
    sijaintikarttaProsessit = static_cast<int (*)[LEVEYS]>(shmat(shmid_map, NULL, 0));
    if (sijaintikarttaProsessit == (void*)-1) { perror("shmat sijaintikartta failed"); return; }

    for (int y = 0; y < KORKEUS; y++)
        for (int x = 0; x < LEVEYS; x++)
            sijaintikarttaProsessit[y][x] = 0;

    // Luo ja alustaa semafori
    semid = semget(sem_key, 1, IPC_CREAT | 0666);
    if (semid == -1) { perror("semget failed"); return; }
    if (semctl(semid, 0, SETVAL, 1) == -1) { perror("semctl SETVAL failed"); return; }

    // Luo lapsiprosessit
    vector<pid_t> child_pids;
    for (int i = 1; i <= ROTTIEN_LKM; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            cout << "Rotta " << i << " (PID: " << getpid() << ") aloittaa reitin etsinnän." << endl;
            aloitaRottaProsessit(i); // Lapsiprosessi suorittaa algoritmin
            // Lapsiprosessi irrottaa vain muistinsa
            if (labyrintti) shmdt(labyrintti);
            if (sijaintikarttaProsessit) shmdt(sijaintikarttaProsessit);
            exit(0);
        } else if (pid > 0) {
            child_pids.push_back(pid);
        } else {
            perror("fork failed");
            return;
        }
    }

    // Odota lapsia
    for (pid_t pid : child_pids) {
        waitpid(pid, NULL, 0);
        cout << "Vanhempiprosessi sai ilmoituksen lapsen (PID: " << pid << ") päättymisestä." << endl;
    }

    // Vanhempiprosessi siivoaa jaetun muistin ja semaforin
    cout << "Kaikki rotat ovat valmiit. Siivotaan jaettu muisti ja semafori." << endl;

    if (labyrintti) {
        if (shmdt(labyrintti) == -1) perror("shmdt labyrintti failed");
        if (shmctl(shmid_laby, IPC_RMID, NULL) == -1) perror("shmctl IPC_RMID labyrintti failed");
    }

    if (sijaintikarttaProsessit) {
        if (shmdt(sijaintikarttaProsessit) == -1) perror("shmdt sijaintikartta failed");
        if (shmctl(shmid_map, IPC_RMID, NULL) == -1) perror("shmctl IPC_RMID sijaintikartta failed");
    }

    if (semid != -1) {
        if (semctl(semid, 0, IPC_RMID) == -1) perror("semctl IPC_RMID failed");
    }

    cout << "Prosessitoteutus valmis." << endl;
}

/**
 * Pääajorutiini säietoteutukselle (KOHTA 2 & 4).
 * Hallinnoi säikeitä ja mutex-lukkoa.
 */
void ajorutiiniSaikeet() {
    // **Alustetaan mutex-lukko (KOHTA 4)**
    if (pthread_mutex_init(&sijainti_mutex, NULL) != 0) {
        cout << "\nMutex init failed\n";
        return;
    }

    // **Alustetaan labyrintti ja sijaintikartta (KOHTA 2 & 4)**
    int example[KORKEUS][LEVEYS] = {
        {1,1,1,1,1,1,1},
        {1,0,1,0,1,0,4},
        {1,0,1,0,1,0,1},
        {1,2,0,2,0,2,1},
        {1,0,1,0,1,0,1},
        {1,0,1,0,1,0,1},
        {1,1,1,3,1,1,1}
    };
    for (int y = 0; y < KORKEUS; y++) {
        for (int x = 0; x < LEVEYS; x++) {
            labyrinttiSaikeet[y][x] = example[y][x];
            sijaintikarttaSaikeet[y][x] = 0;
        }
    }

    pthread_t rotat[ROTTIEN_LKM];
    RatData rottaData[ROTTIEN_LKM];
    
    // **Luodaan säikeet (KOHTA 2)**
    for (int i = 0; i < ROTTIEN_LKM; i++) {
        rottaData[i].id = i + 1;
        cout << "Saie-Rotta " << i + 1 << " aloittaa reitin etsinnän." << endl;
        // Säikeet käyttävät samaa muistiavaruutta (labyrinttiSaikeet, sijaintikarttaSaikeet)
        if (pthread_create(&rotat[i], NULL, aloitaRottaSaikeet, (void*)&rottaData[i]) != 0) {
            perror("pthread_create failed");
            return;
        }
    }

    // **main() -säie odottaa rottasäikeitä (KOHTA 2)**
    for (int i = 0; i < ROTTIEN_LKM; i++) {
        pthread_join(rotat[i], NULL); 
        cout << "main()-säie sai ilmoituksen säikeen (Rotta " << i + 1 << ") päättymisestä." << endl;
    }

    // **Mutexin siivous (KOHTA 4)**
    pthread_mutex_destroy(&sijainti_mutex);

    cout << "Säietoteutus valmis." << endl;
}

//OPISKELIJA: nykyinen main on näin yksinkertainen, tästä pitää muokata se rinnakkainen main
int main(int argc, char* argv[]) {
    srand(time(0));
    cout << "Harjoitus 2: Rotta Labyrintissa" << endl;
    
    // Voit valita, kumman toteutuksen ajat.
    // **KOMMENTOI TOINEN ALLA OLEVA RIVI POIS KÄYTÖSTÄ.**
    
     // ajorutiiniProsessit(); // **Ajaa kohdat 1 ja 3**
     ajorutiiniSaikeet(); // **Ajaa kohdat 2 ja 4**

    return 0;
}