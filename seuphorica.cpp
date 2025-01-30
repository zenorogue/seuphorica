#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include "visutils.h"
#include <emscripten/fetch.h>
#include <random>

const int lsize = 40;

bool game_running;
bool game_restricted;
bool is_daily;
int daily;

int next_id;
int shop_id;
int identifications = 0;

bool scry_active;

enum class language_state { not_fetched, fetch_started, fetch_progress, fetch_success, fetch_fail };

bool enabled_spells, enabled_stay, enabled_power, enabled_id;

struct language {
  language_state state;
  map<int, set<string>> dictionary;
  set<string> naughty;
  string name;
  string gamename;
  string fname;
  string flag;
  int offset, bytes;
  vector<string> alphabet;
  language(const string& name, const string& gamename, const string& fname, const string& alph, const string& flag);
  };

int utf8_len(char ch) {
  unsigned char uch = (unsigned char) ch;
  if(uch < 128) return 1;
  if(uch >= 192 && uch < 224) return 2;
  if(uch >= 224 && uch < 240) return 3;
  if(uch >= 240 && uch < 248) return 4;
  return 5;
  }

int utf8_length(const string& s) {
  int i = 0;
  int len = 0;
  while(i < s.size()) i += utf8_len(s[i]), len++;
  return len;
  }

language english("English", "SEUPHORICA", "wordlist.txt", "ABCDEFGHIJKLMNOPQRSTUVWXYZ", "🇬🇧");
language polski("polski", "SEUFORIKA", "slowa.txt", "AĄBCĆDEĘFGHIJKLŁMNŃOÓPRSŚTUWYZŹŻ", "🇵🇱");
language deutsch("deutsch", "SEUFORIKA", "german.txt", "ABCDEFGHIJKLMNOPQRSTUVWXYZÄÜÖ", "🇩🇪");
language francais("français", "SEUFORICA", "french.txt", "ABCDEFGHIJKLMNOPQRSTUVWXYZ", "🇫🇷");
language espanol("español", "SEUFORICA", "fise-2.txt", "ABCDEFGHIJKLMNÑOPQRSTUVWXYZ", "🇪🇸");
language portugues_br("português (br)", "SEUFORICA", "ptbr.txt", "ABCDEFGHIJLMNOPQRSTUVXZÇ", "🇧🇷");

language *current = &english;

set<language*> polyglot_languages = {};

vector<language*> languages = {&english, &francais, &deutsch, &espanol, &polski, &portugues_br};

language::language(const string& name, const string& gamename, const string& fname, const string& alph, const string& flag) : name(name), gamename(gamename), fname(fname), flag(flag) {
  int i = 0;
  while(i < alph.size()) {
    int len = utf8_len(alph[i]);
    string letter = alph.substr(i, len);
    i += len;
    alphabet.push_back(letter);
    }
  }

void draw_board();

void downloadSucceeded(emscripten_fetch_t *fetch) {
  language& l = *((language*) fetch->userData);
  string s;
  for(int i=0; i<fetch->numBytes; i++) {
    char ch = fetch->data[i];
    if(ch == 10) { l.dictionary[utf8_length(s)].insert(s); s = ""; }
    else s += ch;
    }
  l.dictionary[utf8_length(l.gamename)].insert(l.gamename);
  l.state = language_state::fetch_success;
  emscripten_fetch_close(fetch);
  draw_board();
  }

void downloadSucceeded_naughty(emscripten_fetch_t *fetch) {
  language& l = *((language*) fetch->userData);
  string s;
  for(int i=0; i<fetch->numBytes; i++) {
    char ch = fetch->data[i];
    if(ch == 10) { l.naughty.insert(s); s = ""; }
    else s += ch;
    }
  l.state = language_state::fetch_success;
  emscripten_fetch_close(fetch);
  draw_board();
  }

void downloadFailed(emscripten_fetch_t *fetch) {
  language& l = *((language*) fetch->userData);
  l.state = language_state::fetch_fail;
  emscripten_fetch_close(fetch);
  draw_board();
  }

void downloadProgress(emscripten_fetch_t *fetch) {
  language& l = *((language*) fetch->userData);
  l.state = language_state::fetch_progress;
  l.bytes = fetch->totalBytes;
  l.offset = fetch->dataOffset;
  draw_board();
  }

void read_dictionary(language& l) {
  emscripten_fetch_attr_t attr;
  emscripten_fetch_attr_init(&attr);
  strcpy(attr.requestMethod, "GET");
  attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY | EMSCRIPTEN_FETCH_PERSIST_FILE;
  attr.userData = &l;
  attr.onsuccess = downloadSucceeded;
  attr.onerror = downloadFailed;
  attr.onprogress = downloadProgress;
  l.state = language_state::fetch_started;
  emscripten_fetch(&attr, l.fname.c_str());
  }

void read_naughty_dictionary(language& l) {
  emscripten_fetch_attr_t attr;
  emscripten_fetch_attr_init(&attr);
  strcpy(attr.requestMethod, "GET");
  attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY | EMSCRIPTEN_FETCH_PERSIST_FILE;
  attr.userData = &l;
  attr.onsuccess = downloadSucceeded_naughty;
  attr.onerror = downloadFailed;
  attr.onprogress = downloadProgress;
  l.state = language_state::fetch_started;
  emscripten_fetch(&attr, ("naughty-" + l.fname).c_str());
  }

bool ok(const string& word, int len, language* cur) {
  return cur->dictionary[len].count(word);
  }

bool is_naughty(const string& word, language* cur) {
  return cur->naughty.count(word);
  }

bool not_in_base(const string& letter) {
  for(auto& x: current->alphabet) if(x == letter) return false;
  return true;
  }

bool eqcap(string a, string b) {
  for(char& c: a) c = toupper(c);
  for(char& c: b) c = toupper(c);
  return a == b;
  }

struct polystring_base {
  virtual string get() = 0;
  virtual ~polystring_base() {}
  virtual bool eqcap(const string& s) = 0;
  };

struct polyleaf : public polystring_base {
  string b;
  string get() { return b; }
  polyleaf(const string& s) : b(s) {}
  virtual bool eqcap(const string& s) { return ::eqcap(b, s); }
  };

struct polystring : shared_ptr<polystring_base> {
  polystring() {}
  polystring(const char* s) { ((shared_ptr<polystring_base>&)(*this)) = make_shared<polyleaf> (s); }
  polystring(const string& s) { ((shared_ptr<polystring_base>&)(*this)) = make_shared<polyleaf> (s); }
  operator string() { return ((const shared_ptr<polystring_base>&)(*this))->get(); }
  };

struct translation : public polystring_base {
  language *l;
  polystring in_l;
  polystring not_in_l;
  translation(language *l, polystring in_l, polystring not_in_l) : l(l), in_l(in_l), not_in_l(not_in_l) {}
  string get() { return (((current == l) ? in_l : not_in_l)->get()); }
  virtual bool eqcap(const string& s) { return in_l->eqcap(s) || not_in_l->eqcap(s); }
  };

struct halftrans {
  language *l; polystring in_l;
  halftrans(language *l, polystring in_l) : l(l), in_l(in_l) {}
  };

halftrans in_pl(polystring x) { return halftrans(&polski, x); }
halftrans in_de(polystring x) { return halftrans(&deutsch, x); }
halftrans in_fr(polystring x) { return halftrans(&francais, x); }
halftrans in_es(polystring x) { return halftrans(&espanol, x); }
halftrans in_ptbr(polystring x) { return halftrans(&portugues_br, x); }

polystring operator + (const polystring& s, const halftrans& h) { polystring res; (shared_ptr<polystring_base>&)res = make_shared<translation> (h.l, h.in_l, s); return res; }

ostream& operator << (ostream& os, const polystring& s) { return os << s->get(); }

polystring str_powers = "Powers:" + in_pl("Moce:");
polystring str_rare = "Rare" + in_pl("Rzadkie");
polystring str_epic = "Epic" + in_pl("Epickie");
polystring str_legendary = "Legendary" + in_pl("Legendarne");
polystring str_tiles_in_hand = "Tiles in hand:" + in_pl("Płytki w ręce:");
polystring str_discard = "discard:" + in_pl("odrzucone:");
polystring str_bag= "bag:" + in_pl("worek:");
polystring str_shop_you_have = "Shop: (you have " + in_pl("Sklep: (masz ");
polystring str_cancel_the_purchase = "cancel the purchase" + in_pl("anuluj zakup");
polystring str_turn = "Turn" + in_pl("Runda");
polystring str_total_winnings = "total winnings" + in_pl("łączne wygrane");
polystring str_total_winnings_20 =  "Total winnings until Round 20" + in_pl("łączne wygrane do rundy 20");
polystring str_you_must_finish = "You must finish placing the portal" + in_pl("musisz skończyć ustawiać portal");
polystring str_downloading = "Downloading the dictionary..." + in_pl("Ściągam słownik...");
polystring str_downloading_naughty = "Downloading the naughty dictionary..." + in_pl("Ściągam niegrzeczny słownik...");
polystring str_download_failed = "Failed to download the dictionary!" + in_pl("Nie udało się ściągnąć słownika!");
polystring str_daily = "DAILY" + in_pl("CODZIENNA");
polystring str_play = "play!" + in_pl("graj!");
polystring str_skip_turn = "skip turn!" + in_pl("opuść kolejkę!");
polystring str_view_help = "help" + in_pl("pomoc");
polystring str_dictionary = "dictionary" + in_pl("słownik");
polystring str_game_log = "game log" + in_pl("log gry");
polystring str_new_game = "new game" + in_pl("nowa gra");
polystring str_back_to_game = "back to game" + in_pl("powrót do gry");
polystring str_letter = "letter" + in_pl("litera");
polystring str_value = "value" + in_pl("wartość");
polystring str_special = "special" + in_pl("moc");
polystring str_rarity = "rarity" + in_pl("rzadkość");
polystring str_reverse = "reverse" + in_pl("odwróć");
polystring str_sort_by = "sort by" + in_pl("sortowanie:");
polystring str_you_can_skip = "You can skip your move, but you still need to pay the tax of " + in_pl("Możesz opuścić kolejkę, ale wciąż płacisz podatek ");
polystring str_not_in_dict = "Includes words not in the dictionary!" + in_pl("Tych słów nie ma w słowniku!");
polystring str_flying = "Single flying letters cannot just fly away!" + in_pl("Pojedyncze latające liter nie mogą po prostu odlecieć!");
polystring str_must_cross = "Must cross the existing letters!" + in_pl("Musi się krzyżować z istniejącymi literami!");
polystring str_single_word = "All placed letters must be a part of a single word!" + in_pl("Wszystkie położone litery muszą być w tym samym słowie!");
polystring str_tax = "Tax:" + in_pl("Podatek:");
polystring str_delayed_mult = " Delayed mult: " + in_pl(" opóźniony mnożnik: ");
polystring str_price = "price:" + in_pl("cena:");
polystring str_total_score = "Total score:" + in_pl("Łączny wynik:");
polystring str_not_enough = "You do not score enough to pay the tax!" + in_pl("Za mało, by zapłacić podatek!");
polystring str_illegal = "(illegal word!)" + in_pl("(nielegalne słowo!)");
polystring str_infinite = "Cannot create infinite words" + in_pl("Nie można tworzyć nieskończonych słów");
polystring str_least2 = "Enter at least 2 letters!" + in_pl("Wpisz co najmniej 2 litery");
polystring str_tax_shop_price = "Tax and shop price:" + in_pl("Podatek i ceny w sklepach:");
polystring str_no_matching = "No matching words:" + in_pl("Brak pasujących słów:");
polystring str_matching = "matching words" + in_pl("= liczba pasujących słów");
polystring str_dict_help =
  "Enter the word to check whether it is in the dictionary.</br>"
  "You can use: '.' for any letter, '?' for any letter in hand, '$' for any letter in hand or shop.</br><br/>"
  + in_pl(
  "Wpisz słowo by sprawdzić, czy jest w słowniku.</br>"
  "Możesz użyć: '.' dowolna litera, '?' dowolna litera w ręce, '$' dowolna litera w ręce lub sklepie.</br><br/>"
  );
polystring str_last_game = "Your last game:" + in_pl("Twoja ostatnia gra:");
polystring str_language = "Language:" + in_pl("Twój język:");
polystring str_seed = "seed:" + in_pl("ziarno:");
polystring str_restricted_specials = "Restricted specials:" + in_pl("Ograniczenie mocy:");
polystring str_restrict_example =
  "Examples: <b>8</b> to allow only 8 random special powers; <b>Retain,7</b> to allow only Retain and 7 other special powers; <b>-red,3</b> to allow 3 special powers other than Red; <b>-blue,99</b> to allow all special powers other than Blue<br/>"
  "Also -id to disable identification, -power/-stay/-spell to disable special spots on the map<br/>"
  + in_pl(
  "Przykład: <b>8</b> - 8 losowych mocy; <b>Zatrzymujące,7</b> - Zatrzymujące i 7 innych mocy; <b>-czerwone,3</b> 3 moce ale nie Czerwone; <b>-Niebieskie,99</b> wszystkie moce oprócz Niebieskich<br/>"
  "Poza tym -id by wyłączyć identyfikację, -power/-stay/-spell by wyłączyć specjalne miejsca na mapie<br/>"
  );
polystring str_special_change = "Special letters can change the language to:" + in_pl("Specjalne litery zmieniają język na:");
polystring str_naughty = "naughty tiles" + in_pl("niegrzeczne płytki");
polystring str_welcome = "Welcome to Seuphorica!" + in_pl("Witaj w Seuforice!") + in_es("Bienvenidos a Seuforica!") + in_de("Willkommen bei Seuforica!") + in_fr("bienvenue à Seuforica!")
  +in_ptbr("Bem-vindo à Seuforica!");
polystring str_standard_game = "standard game" + in_pl("gra standardowa");
polystring str_exp_standard_game = "All non-controversial special powers can appear in the game. Play as long as you can!"
  + in_pl("Wszystkie niekontrowersyjne moce mogą pojawić się w grze. Graj tak długo, jak chcesz!");
polystring str_daily_game = "daily game" + in_pl("gra codzienna");
polystring str_time_to_next = "time to the next one: " + in_pl("czas do następnej: ");
polystring str_exp_daily =
    "Win as much 🪙 as you can in 20 turns! Only 8 randomly chosen special powers are available. "
    "Everyone playing today gets the same tiles in shop! Great to compare with other players. "
    "The current daily is #"
  + in_pl(
    "Wygraj jak najwięcej 🪙 w 20 rundach! Tylko 8 losowo wybranych mocy jest dostępna. "
    "Każdy gracz dzisiaj widzi te same płytki w sklepie! Dobry sposób, by się porównać z innymi graczami. "
    "Obecny numer gry codziennej to #");

polystring str_custom_game = "custom game" + in_pl("ustawienia własne");
polystring str_exp_custom_game = "More options." + in_pl("Więcej ustawień.");

polystring str_extra_this = "Extra multipliers this turn: " + in_pl("Dodatkowe mnożniki w tej turze: ");
polystring str_extra_next = "Extra multipliers next turn: " + in_pl("Dodatkowe mnożniki w następnej turze: ") ;
polystring str_extra_debug = "Extra multipliers debug: ";

polystring str_spells = "Spells in library:" + in_pl("Czary w bibliotece:");
polystring str_unidentified = "not identified" + in_pl("niezidentyfikowane");
polystring str_identifications = "Identifications: " + in_pl("Identyfikacje: ");
polystring str_cast_emptyhand = "You cannot cast spells with empty hand." + in_pl("Nie można rzucać czarów z pustą ręką.");
polystring str_cast_identify = "You identify this spell: " + in_pl("Identyfikujesz ten czar: ");
polystring str_cast_zero = "You have currently no copies of this spell: " + in_pl("Nie masz kopii tego czaru.");
polystring str_yields = " yields " + in_pl(" daje: ");

polystring str_spells_need_identify =
  "You have not yet identified the following spells:" + in_pl("Następujące czary jeszcze nie zostały zidentyfikowane:");
polystring str_spells_description =
  "Spells are found or gained via Wizard tiles. They can be cast at any time. Many of them affect your topmost tile, so remember to reorder your hand first! Spells cannot be cast if your hand is empty. Usually, you will not know what a spell does before you use it for the first time."
  + in_pl("Czary znajdujesz albo zdobywasz używając czarodziejskich płytek. Można ja rzucać w dowolnym momencie. Wiele z nich wpływa na najwyższą płytkę, także pamiętaj, by najpierw dobrze ustawić kolejność! Czarów nie można rzucać z pustą ręką. Zazwyczaj nie wiesz, co robi czar przed użyciem go po raz pierwszy.");


struct special {
  polystring caption;
  polystring desc;
  int value;
  unsigned background;
  unsigned text_color;
  };

vector<special> specials = {
  {"No Tile", "", 0, 0xFF000000, 0xFF000000}, 
  {"Placed", "", 0, 0xFFFFFFFF, 0xFF000000},   
  {"Standard" + in_pl("Standardowe"), 
   "no special properties" + in_pl("bez specjalnych własności"),
    0, 0xFFFFFF80, 0xFF000000},

  /* premies */

  {"Premium" + in_pl("Premiowe"),
   "%+d multiplier when used" + in_pl("mnożnik %+d"), 
   1, 0xFFFF8080, 0xFF000000},

  {"Horizontal" + in_pl("Poziome"),
   "%+d multiplier when used horizontally" + in_pl("mnożnik %+d jeśli poziomo"),
   2, 0xFFFF80C0, 0xFF000000},

  {"Vertical" + in_pl("Pionowe"),
   "%+d multiplier when used vertically" + in_pl("mnożnik %+d jeśli pionowo"),
   2, 0xFFFF80C0, 0xFF000000},

  {"Initial" + in_pl("Początkowe"),
   "%+d multiplier when this is the first letter of the word" + in_pl("mnożnik %+d jeśli pierwsza litera w słowie"),
   3, 0xFFFFFFFF, 0xFF808080},

  {"Final" + in_pl("Końcowe"),
   "%+d multiplier when this is the last letter of the word" + in_pl("mnożnik %+d jeśli ostatnia litera w słowie"),
   3, 0xFFFFFFFF, 0xFF808080},

  {"Red" + in_pl("Czerwone"),
   "%+d multiplier when put on a red square" + in_pl("mnożnik %+d jeśli położone na czerwonym polu"),
   4, 0xFFFF2020, 0xFFFFFFFF},

  {"Blue" + in_pl("Niebieskie"),
   "%+d multiplier when put on a blue square" + in_pl("mnożnik %+d jeśli położone na niebieskim polu"),
   3, 0xFF2020FF, 0xFFFFFFFF},

  /* placement */

  {"Flying" + in_pl("Latające"),
   "exempt from the rule that all tiles must be in a single word" + in_pl("ignoruje regułę, że wszystkie płytki muszą być w jednym słowie"),
   0, 0xFF8080FF, 0xFFFFFFFF},

  {"Mirror" + in_pl("Lustrzane"),
   "words going across go down after this letter, and vice versa; %+d multiplier when tiles on all 4 adjacent cells"
   + in_pl("słowa poziomo idą pionowo po tej literze, i odwrotnie; mnożnik %+d jeśli są płytki na wszystkich 4 sąsiednich polach"), 
   3, 0xFF303030, 0xFF4040FF},

  {"Reversing" + in_pl("Odwracające"),
   "words are accepted when written in reverse; if both valid, both score"
   + in_pl("słowa pisane od tyłu są akceptowane; jeśli oba kierunki są poprawne, dostajemy punkty za oba"),
   0, 0xFF404000, 0xFFFFFF80},

  /* unused-tile effects */
  {"Teacher" + in_pl("Uczące"),
   "if used, %+d value to all the unused tiles" + in_pl("jeśli użyte, wartość %+d dla wszystkich nieużytych płytek"),
   1, 0xFFFF40FF, 0xFF000000},

  {"Trasher" + in_pl("Śmieciowe"),
   "all discarded unused tiles, as well as this, are premanently deleted" + in_pl("wszystkie odrzucone nieużyte płytki, a także ta płytka, są trwale usunięte"),
   0, 0xFF000000, 0xFF808080},

  {"Trasher+" + in_pl("Śmieciowe+"),
   "all discarded unused tiles are premanently deleted" + in_pl("wszystkie odrzucone nieużyte płytki są trwale usunięte"),
   0, 0xFF000000, 0xFFFFFFFF},

  {"Duplicator" + in_pl("Podwajające"),
   "%+d copies of all used tiles (but this one is deleted)" + in_pl("%+d kopie wszystkich użytych płytek (ale ta płytka jest usuwana)"),
   1, 0xFFFF40FF, 0xFF00C000},

  {"Retain" + in_pl("Zatrzymujące"),
   "%d first unused tiles are retained for the next turn" + in_pl("%d pierwszych nieużytych płytek przechodzi do kolejnej tury"),
   2, 0xFF905000, 0xFFFFFFFF},

  /* next-turn effects */
  {"Drawing" + in_pl("Ciągnące"),
   "%+d draw in the next round" + in_pl("%+d płytek dobieranych w kolejnej rundzie"),
   2, 0xFFFFC080, 0xFF000000},

  {"Rich" + in_pl("Bogate"),
   "%+d shop choices in the next round (makes tiles appear in the shop faster)"
   + in_pl("%+d płytek w sklepie w kolejnej rundzie (płytki pojawiają się w sklepie szybciej)"),
   6, 0xFFFFE500, 0xFF800000},

  /* other */
  {"Radiating" + in_pl("Promieniujące"),
   "8 adjacent tiles keep their special properties" + in_pl("8 sąsidenich płytek zachowuje swoje moce"),
   0, 0xFF004000, 0xFF80FF80},

  {"Tricky" + in_pl("Trikowe"),
   "all valid subwords including this letter are taken into account for scoring (each counting just once)"
   + in_pl("wszystkie prawidłowe podsłowa zawierające tą płytkę są brane pod uwagę w punktacji (każde słowo tylko raz)"),
   0, 0xFF808040, 0xFFFFFF80},

  {"Soothing" + in_pl("Kojące"),
   "every failed multiplier tile becomes %+d multiplier (does not stack)"
   + in_pl("każda nieudana płytka mnożnikowa zostaje mnożnikiem %+d (nie kumuluje się)"),
   1, 0xFF80FF80, 0xFF008000},

  {"Wild" + in_pl("Dzikie"),
   "you can rewrite the letter while it is in your hand, but it resets the value to 0"
   + in_pl("możesz przepisać literę, gdy ta płytka jest w Twojej ręce, ale to resetuje wartość do 0"),
   0, 0xFF800000, 0xFFFF8000},

  {"Portal" + in_pl("Portalowe"),
   "placed in two locations; teleports between them (max distance %d)"
   + in_pl("kładzione w dwóch miejscach; teleportuje między nimi (maks odległość %d)"),
   6, 0xFF000080, 0xFFFFFFFF},

  {"Wizard" + in_pl("Czarodziejskie"),
   "gives you %d random Spells when used"
   + in_pl("daje Ci %d losowe Czary przy użyciu, za każde słowo"),
   2, 0xFF500050, 0xFFFFFF00},

  {"Redrawing" + in_pl("Ciągnące"),
   "gives you %d redraw Spells when used"
   + in_pl("daje Ci %d Czary Ciągnięcia przy użyciu, za każde słowo"),
   2, 0xFF502050, 0xFFFFFFFF},

  {"Delayed" + in_pl("Opóźnione"),
   "%+d multiplier two turns later, 50%% more for every extra word it is used in"
   + in_pl("mnożnik %+d za dwie kolejki"),
   2, 0xFFC04040, 0xFF400000},

  /* controversial */
  {"Naughty" + in_pl("Niegrzeczne"),
   "%+d multiplier when used in a naughty word" + in_pl("mnożnik %+d gdy użyte w niegrzecznym słowie"),
   5, 0xFF303030, 0xFFFFC0CB},

  /* language */
  {"English", "words in English are accepted. Score twice if valid in both languages. %+d if this letter is not in basic language", 3, 0xFFFFFF80, 0xFF000000},
  {"Deutsch", "Wörter in deutscher Sprache werden akzeptiert. Bei Gültigkeit in beiden Sprachen doppelt punkten. %+d, wenn dieser Buchstabe nicht in der Basissprache vorliegt", 3, 0xFF400000, 0xFFFFFFFF},
  {"Français", "les mots en français sont acceptés. Marquez deux fois si valide dans les deux langues. %+d si cette lettre n'est pas dans la langue de base", 3, 0xFFFFFF80, 0xFF000000},
  {"Español", "Se aceptan palabras en español. Puntuación doble si es válido en ambos idiomas. %+d si esta carta no está en lenguaje básico", 3, 0xFF400000, 0xFFFFFFFF},
  {"Polskie", "słowa po polsku są akceptowane. Wynik liczony dwa razy, jeśli poprawne słowo w obu językach. %+d jeśli ta litera nie jest w języku podstawowym", 3, 0xFFFFFF80, 0xFF000000},
  };

enum class sp {
  notile, placed, standard,

  premium, horizontal, vertical, initial, final, red, blue, 
  flying, bending, reversing,
  teacher, trasher, multitrasher, duplicator, retain,
  drawing, rich,
  radiating, tricky, soothing, wild, portal,
  wizard, redrawing, delayed,

  naughty,

  english, deutsch, francais, espanol, polski, portugues_br,

  first_artifact
  };

struct spell {
  /* unidentified name */
  polystring color;
  /* color */
  unsigned color_value;
  /* identified name -- scrambled */
  polystring caption;
  /* identified description -- scrambled */
  polystring desc;
  /* action performed on cast -- scrambled */
  std::function<void()> action;
  /* action id, for given color/index */
  int action_id;
  /* color id, for given action/caption/desc/inventory/identified */
  int color_id;
  /* number of spells of this color in inventory */
  int inventory;
  /* are spells of this color identified? */
  bool identified;
  /* the Greek letter representing this spell */
  string greek;
  };

extern vector<spell> spells;

array<bool, (int) sp::first_artifact> special_allowed;

stringstream game_log;

void add_to_log(const string& s) {
  game_log << s << "<br/>\n";
  }

map<sp, vector<sp>> artifacts;

struct tile {
  int id;
  string letter;
  int value;
  sp special;
  int price;
  int rarity;
  tile(string l, sp special, int value = 1) : letter(l), value(value), special(special) {
    id = next_id++; price = 0; rarity = 1;
    }
  };

struct coord {
  int x, y;
  coord(int x, int y) : x(x), y(y) {}
  coord operator + (const coord& b) const { return coord(x+b.x, y+b.y); }
  coord operator - (const coord& b) const { return coord(x-b.x, y-b.y); }
  coord operator - () const { return coord(-x, -y); }
  bool operator < (const coord& b) const { return tie(x,y) < tie(b.x, b.y); }
  bool operator == (const coord& b) const { return tie(x,y) == tie(b.x, b.y); }
  coord mirror() { return coord(y, x); }
  };

int gmod(int a, int b) {
  a %= b; if(a<0) a += b; return a;
  }

set<coord> just_placed;

map<coord, tile> board;

enum eBoardEffect { beNone, beRed, beBlue, bePower, beStay, beSpell = 64 };

map<coord, eBoardEffect> colors;

set<vector<coord>> old_tricks;

map<coord, coord> portals;

bool placing_portal;
coord portal_from(0, 0);

int gameseed;

std::mt19937 draw_rng;
std::mt19937 shop_rng;
std::mt19937 board_rng;
std::mt19937 spells_rng;

int hrand(int i, std::mt19937& which = shop_rng) {
  unsigned d = which() - which.min();
  long long m = (long long) (which.max() - which.min()) + 1;
  m /= i;
  d /= m;
  if(d < (unsigned) i) return d;
  return hrand(i);
  }

int hrand_once(int i, std::mt19937& which = shop_rng) {
  unsigned d = which() - which.min();
  long long m = (long long) (which.max() - which.min()) + 1;
  m /= i;
  d /= m;
  if(d == i) return i-1;
  return d;
  }

vector<int> board_cache;

bool colors_swapped;

eBoardEffect get_color(coord c) {
  bool has_red = special_allowed[(int) sp::red];
  bool has_blue = special_allowed[(int) sp::blue];
  // if(!has_red && !has_blue) return 0;
  bool has_swap = special_allowed[(int) sp::wizard] || enabled_spells;
  if(enabled_stay && has_swap) has_blue = true;
  if(enabled_power && has_swap) has_red = true;

  if(!colors.count(c)) { 
    int ax = abs(c.x), ay = abs(c.y);
    int index = (ax+ay) * (ax+ay) + ax;
    index *= 4;
    if(c.x > 0) index++; if(c.y > 0) index+=2;
    while(board_cache.size() <= index) {
      board_cache.push_back(hrand(25, board_rng));
      if(board_cache.back() == 18 || board_cache.back() == 17) if(enabled_spells) board_cache.back() = 64 + hrand(spells.size(), board_rng);
      }
    int r = board_cache[index];
    if(r == 24) colors[c] = beRed;
    else if(r == 22 || r == 23) colors[c] = beBlue;
    else if(r == 21) colors[c] = bePower;
    else if(r == 20 || r == 19) colors[c] = beStay;
    else if(r >= 64) colors[c] = eBoardEffect(beSpell + (r - 64));
    else colors[c] = beNone;


    if(colors[c] == beRed && !has_red) colors[c] = beNone;
    if(colors[c] == beBlue && !has_blue) colors[c] = beNone;
    if(colors[c] == beStay && !enabled_stay) colors[c] = beNone;
    if(colors[c] == bePower && !enabled_power) colors[c] = beNone;
    }
  auto res = colors[c];
  if(res == beRed && colors_swapped && enabled_power) res = bePower;
  else if(res == bePower && colors_swapped) res = beRed;
  else if(res == beBlue && colors_swapped && enabled_stay) res = beStay;
  else if(res == beStay && colors_swapped) res = beBlue;
  return res;
  }

/* note: if spell[1].action_id is 2, then spell[1].inventory refers to the number of held spells which perform spell[2].action, similarly spell[1].identified */

std::vector<std::string> greek_letters = {"α", "β", "γ", "δ", "ε", "ζ", "η", "θ", "ι", "κ", "λ", "μ", "ν", "ξ", "ο", "π", "ρ", "σ", "τ", "υ", "φ", "χ", "ψ", "ω"};

string color_to_str(unsigned col) {
  char buf[10];
  snprintf(buf, 8, "%06X", col);
  return buf;
  }

vector<tile> drawn;
vector<tile> deck;
vector<tile> discard;
vector<tile> shop;

array<int, 3> stacked_mults;

void spell_message(const string&);
string last_spell_effect;

tile empty_tile(0, sp::notile);

special &gsp(sp x) {
  return specials[int(x)];
  }

special &gsp(const tile &t) {
  return gsp(t.special);
  }

string power_description(const special& s, int rarity) {
  char buf[127];
  sprintf(buf, s.desc->get().c_str(), s.value * rarity);
  return buf;
  }

string power_description(const tile &t) {
  if(t.special >= sp::first_artifact) {
    stringstream ss;
    int qty = 0;
    for(auto p: artifacts[t.special]) {
      if(qty) ss << "; "; qty++;
      ss << power_description(gsp(p), t.rarity);
      }
    return ss.str();
    }
  return power_description(gsp(t), t.rarity);
  }

string short_desc(const tile& t) {
  auto& s = gsp(t);
  string out = s.caption;
  if(t.rarity == 2) out = str_rare->get() + " " + out;
  if(t.rarity == 3) out = str_epic->get() + " " + out;
  if(t.rarity == 4) out = str_legendary->get() + " " + out;
  out += " ";
  out += t.letter;
  out += to_string(t.value);
  return out;
  }

string tile_desc(const tile& t) {
  auto& s = gsp(t);
  string out;
  string cap = s.caption;
  cap += " ";
  cap += t.letter;
  cap += to_string(t.value);
  if(t.special >= sp::first_artifact && t.rarity == 2)
    out = "<b><font color='#FFA500'>" + string(str_legendary) + " " + cap + ": </font></b>";
  else if(t.special >= sp::first_artifact)
    out = "<b><font color='#FFD500'>" + cap + ": </font></b>";
  else if(t.rarity == 1)
    out = "<b>" + cap + ": </b>";
  else if(t.rarity == 2)
    out = "<b><font color='#4040FF'>" + string(str_rare) + " " + cap + ": </font></b>";
  else if(t.rarity == 3)
    out = "<b><font color='#FF40FF'>" + string(str_epic) + " " + cap + ": </font></b>";
  else if(t.rarity == 4)
    out = "<b><font color='#FFA500'>" + string(str_legendary) + " " + cap + ": </font></b>";
  out += power_description(t);
  if(t.price) out += " (" + to_string(t.price) + " 🪙)";
  return out;
  }

bool has_power(const tile& t, sp which, int& val) {
  auto& s = gsp(t);
  if(t.special == which) { val = s.value * t.rarity; return true; }
  if(t.special >= sp::first_artifact) {
    const auto& artifact = artifacts[t.special];
    for(auto w: artifact) if(w == which) { val = gsp(w).value * t.rarity; return true; }
    }
  return false;
  }

bool has_power(const tile& t, sp which) {
  int dummy; return has_power(t, which, dummy);
  }

language *get_language(sp s) {
  if(s == sp::polski) return &polski;
  if(s == sp::english) return &english;
  if(s == sp::deutsch) return &deutsch;
  if(s == sp::francais) return &francais;
  if(s == sp::espanol) return &espanol;
  if(s == sp::portugues_br) return &portugues_br;
  return nullptr;
  }

language *get_language(tile &t, int& val) {
  if(has_power(t, sp::english, val)) return &english;
  if(has_power(t, sp::polski, val)) return &polski;
  if(has_power(t, sp::deutsch, val)) return &deutsch;
  if(has_power(t, sp::francais, val)) return &francais;
  if(has_power(t, sp::espanol, val)) return &espanol;
  if(has_power(t, sp::portugues_br, val)) return &portugues_br;
  return nullptr;
  }

language *get_language(tile &t) {
  int dummy; return get_language(t, dummy);
  }

string alphashift(tile& t, int val) {
  auto lang = get_language(t);
  if(!lang) lang = current;
  auto& a = lang->alphabet;
  int size = a.size();

  for(int i=0; i<size; i++)
    if(a[i] == t.letter)
      return a[gmod(i + val, size)];

  return t.letter;
  }

void render_tile(pic& p, int x, int y, tile& t, const string& onc) {
  auto& s = gsp(t);
  unsigned lines = 0xFF000000;
  int wide = 1;
  if(t.rarity == 2) lines = 0xFF4040FF, wide = 2;
  if(t.rarity == 3) lines = 0xFFFF80FF, wide = 2;
  if(t.special >= sp::first_artifact) lines = 0xFFFFD500, wide = 2;
  style b(lines, s.background, 1.5 * wide);
  style bempty(0xFF808080, 0xFF101010, 0.5);

  path pa(t.special != sp::notile ? b : bempty);
  pa.add(vec(x, y));
  pa.add(vec(x+lsize, y));
  pa.add(vec(x+lsize, y+lsize));
  pa.add(vec(x, y+lsize));
  pa.onclick = onc;
  pa.cycled = true;
  p += pa;

  int l1 = lsize*1/10;
  int l9 = lsize*9/10;
  int l7 = lsize*7/10;

  if(has_power(t, sp::bending)) {
    style bmirror(0xFFC0C0FF, 0, 5);
    path pa1(bmirror);
    pa1.add(vec(x, y));
    pa1.add(vec(x+l9, y+l9));
    pa1.onclick = onc;
    p += pa1;
    }

  if(has_power(t, sp::portal)) {
    style bmirror(0xFFFF8000, 0, 5);
    path pa1(bmirror);
    pa1.add(vec(x+l1, y+l9));
    pa1.add(vec(x+l9, y+l1));
    pa1.onclick = onc;
    p += pa1;
    }

  if(has_power(t, sp::horizontal)) {
    style bhori(0xFFFFFFFF, 0, 5);
    for(int a: {l1, l9}) {
      path pa1(bhori);
      pa1.add(vec(x+l1, y+a));
      pa1.add(vec(x+l9, y+a));
      pa1.onclick = onc;
      p += pa1;
      }
    }

  if(has_power(t, sp::vertical)) {
    style bhori(0xFFFFFFFF, 0, 5);
    for(int a: {l1, l9}) {
      path pa1(bhori);
      pa1.add(vec(x+a, y+l1));
      pa1.add(vec(x+a, y+l9));
      pa1.onclick = onc;
      p += pa1;
      }
    }

  if(has_power(t, sp::initial)) {
    style bhori(0xFF000000, 0, 3);
    path pa1(bhori);
    pa1.add(vec(x+l1, y+l9));
    pa1.add(vec(x+l1, y+l1));
    pa1.add(vec(x+l9, y+l1));
    pa1.add(vec(x+l1, y+l1));
    p += pa1;
    }

  if(has_power(t, sp::final)) {
    style bhori(0xFF000000, 0, 3);
    path pa1(bhori);
    pa1.add(vec(x+l9, y+l1));
    pa1.add(vec(x+l9, y+l7));
    p += pa1;
    pa1.lst.clear();
    pa1.add(vec(x+l1, y+l9));
    pa1.add(vec(x+l7, y+l9));
    p += pa1;
    }

  language *lang = get_language(t);
  if(lang) {
    font ff = makefont("DejaVuSans-Bold.ttf", ";font-family:'DejaVu Sans';font-weight:bold");
    style bblack(0, s.text_color, 0);
    text t1(bblack, ff, vec(x+lsize*.5, y+lsize*.35), center, lsize * .9, lang->flag);
    t1.onclick = onc;
    p += t1;
    }

  if(t.special != sp::notile) {
    font ff = makefont("DejaVuSans-Bold.ttf", ";font-family:'DejaVu Sans';font-weight:bold");
    style bblack(0, s.text_color, 0);
    text t1(bblack, ff, vec(x+lsize*.45, y+lsize*.35), center, lsize*.9, t.letter);
    t1.onclick = onc;
    p += t1;
    string s = to_string(t.value);
    text t2(bblack, ff, vec(x+lsize*.95, y+lsize*.95), botright, lsize*.3, s);
    t2.onclick = onc;
    p += t2;
    }
  }

string spell_desc(int id, int qty = -1) {
  auto& sp = spells[id];
  auto& sp2 = spells[sp.action_id];

  stringstream ss;
  if(qty != -1) ss << "<a onclick='cast_spell(" << id << ")'>";
  ss << "<font color=\"" << color_to_str(sp.color_value) << "\">" << sp.greek;
  if(sp.identified) ss << " " << sp2.caption;
  else ss << " " << sp.color;
  ss << "</font>";
  if(qty != -1) ss << "</a>";
  if(qty != -1) ss << " (x" << qty << ")";
  if(sp.identified) ss << ": " << sp2.desc;
  else ss << ": " << str_unidentified;
  return ss.str();
  }

int cash = 80;

int roundindex = 1;
int total_gain = 0;
int total_gain_20 = 0;

int taxf(int r) {
  double rd = r - 1;
  return (5 + rd) * (10 + rd) * (10 + rd) * (20 + rd) * (40 + rd) / 80000;
  }

int tax() { return taxf(roundindex); }

int get_max_price(int r) {
  return (9 + r) * (9 + r) * (25 + r) / 250;
  }

int get_min_price(int r) {
  return (1+get_max_price(r)) / 2;
  }

map<string, int> word_use_count;

struct eval {
  int total_score;
  string current_scoring;
  bool valid_move;
  set<coord> used_tiles;
  set<vector<coord>> new_tricks;
  vector<string> used_words;
  int qdelay;
  };

eval ev;

void compute_score() {

  auto langs = polyglot_languages; langs.insert(current);
  for(auto lang: langs) {
    if(lang->state == language_state::not_fetched) {
      read_dictionary(*lang);
      }
    if(lang->state == language_state::fetch_started) {
      ev.current_scoring = str_downloading;
      ev.valid_move = false;
      return;
      }
    if(lang->state == language_state::fetch_progress) {
      int bytes = lang->bytes;
      double pct = lang->offset * 1. / bytes;
      char buf[64];
      snprintf(buf, 64, "%4.1f%% of %.1f MiB", pct * 100, bytes / 1048576.);
      ev.current_scoring = str_downloading;
      ev.current_scoring += buf;
      ev.valid_move = false;
      return;
      }

    if(lang->state == language_state::fetch_fail) {
      ev.current_scoring = str_download_failed;
      ev.valid_move = false;
      return;
      }

    if(lang->state == language_state::fetch_success && lang->naughty.empty()) {
      read_naughty_dictionary(*lang);
      ev.current_scoring = str_downloading_naughty;
      ev.valid_move = false;
      return;
      }
    }

  if(placing_portal) { ev.current_scoring = str_you_must_finish;  ev.valid_move = false; return; }

  set<pair<coord, coord>> starts;

  for(auto p: just_placed) {
    for(coord dir: { coord(1,0), coord(0,1) }) {
      coord prev = -dir;
      auto &t = board.at(p);
      if(has_power(t, sp::bending)) prev = prev.mirror();
      auto p1 = p;
      if(has_power(t, sp::portal)) p = portals.at(p);
      bool seen_tricky = false;
      if(board.count(p1+dir) || board.count(p+prev)) {
        int steps = 0;
        auto at = p;
        while(board.count(at + prev)) {
          steps++; if(steps >= 10000) { ev.current_scoring = str_infinite; ev.valid_move = false; return; }
          auto &ta = board.at(at);
          if(has_power(ta, sp::tricky)) seen_tricky = true;
          if(seen_tricky) starts.emplace(at, -prev);
          at = at + prev;
          auto &ta1 = board.at(at);
          if(has_power(ta1, sp::bending)) prev = prev.mirror();
          if(has_power(ta1, sp::portal)) at = portals.at(at);
          }
        starts.emplace(at, -prev);
        }
      }
    }

  ev.total_score = 0;
  ev.valid_move = just_placed.empty();
  ev.used_tiles.clear();
  ev.qdelay = 0;
  ev.new_tricks.clear();
  ev.used_words.clear();

  bool illegal_words = false;

  bool is_crossing = false;

  bool fly_away = false;
  for(auto p: just_placed) if(has_power(board.at(p), sp::flying)) {
    fly_away = true;
    for(auto v: {coord(1,0), coord(-1,0), coord(0,1), coord(0,-1)}) if(board.count(p+v)) fly_away = false;
    if(fly_away) break;
    }

  stringstream scoring;
  for(auto ss: starts) {
    auto at = ss.first, next = ss.second;
    string word;
    set<coord> needed;
    for(auto p: just_placed) if(!has_power(board.at(p), sp::flying)) needed.insert(p);
    int index = 0;
    bool has_tricky = false;
    int directions = 1;
    bool optional = board.count(at-next);
    vector<coord> allword;
    int rmul = 1 + stacked_mults[roundindex % 3];
    int sooth = 0, rsooth = 0;
    set<language*> polyglot = { current };

    /* data shared between both directions */
    struct eval_data_shared {
      int naughtymul = 0, naughtysooth = 0, qsooth = 0;
      int placed = 0, all = 0, start_delay = 0;
      } eds;

    /* data in given direction (direct/reverse) */
    struct eval_data_directed {
      int mul, sooth;
      string word;
      eval_data_directed() { mul = 1 + stacked_mults[roundindex % 3]; sooth = 0; }

      void evaluate(eval_data_shared& eds, language *l, stringstream& scoring, bool illegal) {
        int mul1 = mul + eds.qsooth * sooth + (is_naughty(word, l) ? eds.naughtymul : eds.naughtysooth * sooth) - word_use_count[word];
        int score = eds.placed * eds.all * mul1;

        scoring << "<b>" << word << ":</b> " << eds.placed << "*" << eds.all << "*" << mul1 << " = " << score;
        if(l != current) scoring << " " << l->flag;
        if(illegal) scoring << " <font color='#FF4040'>" << str_illegal << "</font>";
        scoring << "<br/>";
        ev.used_words.push_back(word);
        ev.total_score += score;
        ev.qdelay += eds.start_delay;
        }
      } edd, edr;

    while(board.count(at)) {
      needed.erase(at);
      ev.used_tiles.insert(at);
      auto& b = board.at(at);
      edd.word += b.letter;
      edr.word = b.letter + edr.word;
      allword.push_back(at);

      if(just_placed.count(at)) eds.placed += b.value;
      else is_crossing = true;

      eds.all += b.value;
      int val = gsp(b).value * b.rarity;

      auto affect_mul = [&] (bool b, int ways = 3) {
        if(b) { if(ways & 1) edd.mul += val; if(ways & 2) edr.mul += val; }
        if(!b) { if(ways & 1) edd.sooth++; if(ways & 2) edr.sooth++; }
        };

      auto b1 = b; if(get_color(at) == bePower) b1.rarity++;

      if(has_power(b1, sp::tricky, val)) has_tricky = true;
      if(has_power(b1, sp::soothing, val)) eds.qsooth = max(eds.qsooth, val);
      if(has_power(b1, sp::reversing, val)) directions = 2;
      if(has_power(b1, sp::bending, val)) next = next.mirror();
      if(has_power(b1, sp::portal, val)) { at = portals.at(at); ev.used_tiles.insert(at); needed.erase(at); }
      if(has_power(b1, sp::premium, val)) affect_mul(true);
      if(has_power(b1, sp::horizontal, val)) affect_mul(next.x);
      if(has_power(b1, sp::vertical, val)) affect_mul(next.y);
      if(has_power(b1, sp::naughty, val)) { eds.naughtymul += val; eds.naughtysooth++; }
      if(has_power(b1, sp::red, val)) affect_mul(get_color(at) == beRed);
      if(has_power(b1, sp::blue, val)) affect_mul(get_color(at) == beBlue);
      if(has_power(b1, sp::initial, val)) { affect_mul(index == 0, 1); }
      if(has_power(b1, sp::final, val)) { affect_mul(index == 0, 2); }
      if(has_power(b1, sp::delayed, val)) { eds.start_delay += val; }
      if(has_power(b1, sp::bending, val))
        affect_mul(board.count(at-coord(1,0)) && board.count(at+coord(1,0)) && board.count(at-coord(0,1)) && board.count(at+coord(0,1)));

      auto lang = get_language(b, val);
      if(lang) { polyglot.insert(lang); affect_mul(not_in_base(b.letter)); }

      at = at + next;
      if(has_power(b1, sp::final, val)) edd.mul += val;
      if(has_power(b1, sp::initial, val)) edr.mul += val;
      index++;
      if(has_tricky && board.count(at) && !old_tricks.count(allword)) {
        for(int rd=0; rd<directions; rd++) for(auto l: polyglot) {
          auto& mdc = rd ? edr : edd;
          if(ok(mdc.word, index, l)) {
            mdc.evaluate(eds, l, scoring, false);
            ev.new_tricks.insert(allword);
            }
          }
        }
      if(has_power(b1, sp::final, val)) edd.mul -= val;
      if(has_power(b1, sp::initial, val)) edr.mul -= val;
      if(has_power(b1, sp::final, val)) affect_mul(!board.count(at), 1);
      if(has_power(b1, sp::initial, val)) affect_mul(!board.count(at), 2);
      }
    if(needed.empty()) ev.valid_move = true;
    bool is_legal = false;

    for(int rd=0; rd<directions; rd++) for(auto l: polyglot) {
      auto& mdc = rd ? edr : edd;
      if(ok(mdc.word, index, l)) {
        mdc.evaluate(eds, l, scoring, false);
        is_legal = true;
        }
      }

    if(!is_legal && optional) continue;
    if(!is_legal) {
      edd.evaluate(eds, current, scoring, true);
      illegal_words = true;
      }
    }

  if(just_placed.empty()) { scoring << str_you_can_skip << tax() << " 🪙."; }
  else if(illegal_words) { scoring << str_not_in_dict; ev.valid_move = false; }
  else if(fly_away) { scoring << str_flying; ev.valid_move = false; }
  else if(!is_crossing) { scoring << str_must_cross; ev.valid_move = false; }
  else if(!ev.valid_move) scoring << str_single_word;
  else scoring << str_total_score << " " << ev.total_score << " 🪙 " << str_tax << " " << tax() << " 🪙";

  for(auto& p: ev.used_tiles) {
    auto& b = board.at(p);
    auto b1 = b; if(get_color(p) == bePower) b1.rarity++;
    int val;
    if(has_power(b1, sp::delayed, val)) ev.qdelay += val;
    }
  ev.qdelay /= 2;

  if(ev.qdelay) scoring << str_delayed_mult << ev.qdelay;

  if(cash + ev.total_score < tax()) { scoring << "<br/>" << str_not_enough; ev.valid_move = false; }

  for(auto ut: just_placed) {
    auto c =get_color(ut);
    switch(c) {
      case beRed:
        if(has_power(board.at(ut), sp::red))
          scoring << "<br/>" << short_desc(board.at(ut)) << (" uses its Red power" + in_pl(" używa Czerwonej mocy"));
        else
          scoring << "<br/>" << short_desc(board.at(ut)) << (" has no Red power" + in_pl(" nie ma Czerwonej mocy"));
        break;
      case beBlue:
        if(has_power(board.at(ut), sp::blue))
          scoring << "<br/>" << short_desc(board.at(ut)) << (" uses its Blue power" + in_pl(" używa Niebieskiej mocy"));
        else
          scoring << "<br/>" << short_desc(board.at(ut)) << (" has no Blue power" + in_pl(" nie ma Niebieskiej mocy"));
        break;
      case beStay:
        scoring << "<br/>" << short_desc(board.at(ut)) << (" stays on the board (and is permanently removed from your deck)" + in_pl(" zostaje na planszy (i jest trwale usunięte z talii)"));
        break;
      case bePower: {
        auto x = board.at(ut);
        scoring << "<br/>" << short_desc(x);
        x.rarity++;
        scoring << (" acts as " + in_pl(" działa jako ")) << tile_desc(x);
        break;
        }
      default: ;
      }
    if(c >= beSpell) {
      scoring << "<br/>" << short_desc(board.at(ut)) << (" gains " + in_pl(" zdobywa ")) << spell_desc(c - beSpell);
      }
    }

  ev.current_scoring = scoring.str();
  };

string power_list() {
  stringstream ss;
  ss << str_powers;
  bool next = false;
  for(int i=0; i< (int) sp::first_artifact; i++) if(special_allowed[i]) {
    if(next) ss << ","; next = true;
    ss << " " << specials[i].caption;
    }
  return ss.str();
  }

void draw_board() {
  pic p;

  compute_score();

  int minx=15, miny=15, maxx=0, maxy=0;
  for(auto& b: board) if(!just_placed.count(b.first)) minx = min(minx, b.first.x), maxx = max(maxx, b.first.x), miny = min(miny, b.first.y), maxy = max(maxy, b.first.y);
  miny -= 6; minx -= 6; maxx += 7; maxy += 7;

  for(int y=miny; y<maxy; y++)
  for(int x=minx; x<maxx; x++) {
    string s = " onclick = 'drop_hand_on(" + to_string(x) + "," + to_string(y) + ")'";
    render_tile(p, x*lsize, y*lsize, empty_tile, s);
    int c = get_color({x, y});
    if(c == beRed || c == beBlue) {
      style bred(0, 0xFFFF0000, 0);
      style bblue(0, 0xFF0000FF, 0);
      path pa(c == beRed ? bred : bblue);
      pa.add(vec(x*lsize+lsize/2, y*lsize+lsize/4));
      pa.add(vec(x*lsize+lsize/4, y*lsize+lsize/2));
      pa.add(vec(x*lsize+lsize/2, y*lsize+lsize*3/4));
      pa.add(vec(x*lsize+lsize*3/4, y*lsize+lsize/2));
      pa.onclick = s;
      pa.cycled = true;
      p += pa;
      }

    font ff = makefont("DejaVuSans-Bold.ttf", ";font-family:'DejaVu Sans';font-weight:bold");
    if(c == bePower) {
      style bpower(0, 0xFF408040, 0);
      text t1(bpower, ff, vec(x*lsize+lsize*.5, y*lsize+lsize*.4), center, lsize*.75, "+");
      t1.onclick = s;
      p += t1;
      }

    if(c == beStay) {
      style bstay(0, 0xFF303030, 0);
      text t1(bstay, ff, vec(x*lsize+lsize*.5, y*lsize+lsize*.4), center, lsize*.75, "⨯");
      t1.onclick = s;
      p += t1;
      }

    if(c >= beSpell) {
      spell& sp = spells[c - beSpell];
      style bpower(0, 0xFF000000 | sp.color_value, 0);
      text t1(bpower, ff, vec(x*lsize+lsize*.5, y*lsize+lsize*.4), center, lsize*.5, sp.greek);
      t1.onclick = s;
      p += t1;
      }

    }

  for(int y=miny; y<maxy; y++)
  for(int x=minx; x<maxx; x++) if(board.count({x, y})) {
    string s ="";
    if(just_placed.count({x, y})) s = " onclick = 'back_from_board(" + to_string(x) + "," + to_string(y) + ")'";
    render_tile(p, x*lsize, y*lsize, board.at({x,y}), s);
    }

  for(int y=miny; y<maxy; y++)
  for(int x=minx; x<maxx; x++) if(portals.count({x, y})) {
    auto c1 = portals.at({x, y});
    int l1 = lsize*1/10;
    int l9 = lsize*9/10;
    style borange(0x80FF8000, 0, 3);
    style bblue(0x800000FF, 0, 3);
    path pa1(borange);
    pa1.add(vec(x*lsize + l1, y*lsize+l9));
    pa1.add(vec(c1.x*lsize + l1, c1.y*lsize+l9));
    p += pa1;
    path pa2(bblue);
    pa2.add(vec(x*lsize + l9, y*lsize+l1));
    pa2.add(vec(c1.x*lsize + l9, c1.y*lsize+l1));
    p += pa2;
    }

  stringstream ss;

  ss << "<center>\n";

  ss << SVG_to_string(p);

  ss << "</center><br/><br/>";

  ss << "<div style=\"float:left;width:20%\">&nbsp;</div>";
  ss << "<div style=\"float:left;width:20%\">";

  ss << "<b>" << str_tiles_in_hand << " </b>(<a onclick='check_discard()'>" << str_discard << " " << discard.size() << " " << str_bag << " " << deck.size() << "</a>)<br/>";

  int id = 0;
  for(auto& t: drawn) {
    pic p;
    string s =" onclick='draw_to_hand(" + to_string(id++) + ")'";
    render_tile(p, 0, 0, t, s);
    string sts = SVG_to_string(p);
    // int pos = sts.find("svg");
    // sts.insert(pos+4, "draggable=\"true\" ondragstart=\"drag(event)\" onclick=\"alert('clicked!')\" ");
    ss << sts + " " + tile_desc(t);
    if(has_power(t, sp::wild)) {
      for(char ch='A'; ch <= 'Z'; ch++)
        ss << " <a onclick='wild_become(" << id-1 << ", \"" << ch << "\")'>" << ch << "</a>";
      }
    ss << "<br/>";
    }

  bool have_spells = false;
  for(auto& sp: spells) if((sp.identified && enabled_id) || sp.inventory) have_spells = true;
  if(have_spells) {
    ss << "<b>" << str_spells << "</b><br/>";
    for(int id=0; id<int(spells.size()); id++) {
      auto& sp = spells[id];
      if((sp.identified && enabled_id) || sp.inventory) {
        ss << spell_desc(id, sp.inventory) << "<br/>";
        }
      }
    if(last_spell_effect != "") ss << last_spell_effect << "<br/>";
    if(identifications) ss << str_identifications << identifications << "<br/>";
    ss << "<br/>";
    }
  ss << "</div>";

  ss << "<div style=\"float:left;width:20%\">";
  ss << "<b>" << str_shop_you_have << cash << " 🪙)</b><br/>";

  id = 0;
  for(auto& t: shop) {
    pic p;
    string s =" onclick='buy(" + to_string(id++) + ")'";
    render_tile(p, 0, 0, t, s);
    string sts = SVG_to_string(p);
    // int pos = sts.find("svg");
    // sts.insert(pos+4, "draggable=\"true\" ondragstart=\"drag(event)\" onclick=\"alert('clicked!')\" ");
    ss << sts + " " + tile_desc(t) + " <br/>";
    }
  if(drawn.size() && drawn[0].price) {
    pic p;
    render_tile(p, 0, 0, empty_tile, " onclick='back_to_shop()'");
    ss << SVG_to_string(p) << " " << str_cancel_the_purchase << "<br/>";
    }
  ss << "</div>";
  ss << "<div style=\"float:left;width:20%\">";

  if(is_daily) ss << str_daily << " #" << daily << "<br>";
  if(game_restricted) {
    ss << power_list() << "<br/>";
    }
  int sm0 = stacked_mults[roundindex%3];
  int sm1 = stacked_mults[(roundindex+1)%3];
  int sm2 = stacked_mults[(roundindex+2)%3];
  if(sm0) { ss << str_extra_this << sm0 << "<br/>"; }
  if(sm1) { ss << str_extra_next << sm1 << "<br/>"; }
  if(sm2) { ss << str_extra_debug << sm2 << "<br/>"; }
  ss << str_turn << ": " << roundindex << " " << str_total_winnings << ": " << total_gain << " 🪙<br/>";
  if(roundindex > 20) ss << str_total_winnings_20 << ": " << total_gain_20 << " 🪙<br/>";
  ss << ev.current_scoring << "<br/><br/>";
  if(ev.valid_move && just_placed.empty()) {
    add_button(ss, "play()", str_skip_turn);
    ss << "<br/>";
    }
  else if(ev.valid_move) {
    add_button(ss, "play()", str_play);
    ss << "<br/>";
    }
  ss << "<hr/>";
  ss << "<a onclick='view_help()'>" << str_view_help << "</a>";
  ss << " - <a onclick='view_dictionary()'>" << str_dictionary << "</a>";
  ss << " - <a onclick='view_game_log()'>" << str_game_log << "</a>";
  ss << "<br/><a onclick='view_new_game()'>" << str_new_game << "</a>";
  ss << "</div></div>";
  ss << "</div>";

  set_value("output", ss.str());
  }

extern "C" {

void check_discard() {
  stringstream ss;

  ss << "<div style=\"float:left;width:10%\">&nbsp;</div>";
  ss << "<div style=\"float:left;width:40%\">"; 
  ss << "<b>" << str_discard << "</b><br/>";
  for(auto& t: discard) {
    pic p;
    render_tile(p, 0, 0, t, "");
    ss << SVG_to_string(p) << " " << tile_desc(t) << " <br/>";
    }
  ss << "<a onclick='back_to_game()'>" << str_back_to_game << "</a> - " << str_sort_by << " ";
  ss << "<a onclick='sort_by(1)'>" << str_letter << "</a>";
  ss << " / <a onclick='sort_by(2)'>" << str_value << "</a>";
  ss << " / <a onclick='sort_by(3)'>" << str_special << "</a>";
  ss << " / <a onclick='sort_by(4)'>" << str_rarity << "</a>";
  ss << " / <a onclick='sort_by(5)'>" << str_reverse << "</a>";
  ss << "</div>";
  ss << "<div style=\"float:left;width:40%\">"; 
  ss << "<b>" << str_bag << "</b><br/>";
  for(auto& t: deck) {
    pic p;
    render_tile(p, 0, 0, t, "");
    ss << SVG_to_string(p) << " " << tile_desc(t) << " <br/>";
    }
  ss << "<a onclick='back_to_game()'>" << str_back_to_game << "</a>";
  ss << "</div>"; 
  ss << "</div>"; 

  set_value("output", ss.str());
  }

void sort_by(int i) {
  auto f = [i] (const tile &t1, const tile& t2) {
    if(i == 1) return t1.letter < t2.letter;
    if(i == 2) return t1.value > t2.value;
    if(i == 3) return t1.special < t2.special;
    if(i == 4) return t1.rarity > t2.rarity;
    return false;
    };
  if(i == 5) {
    reverse(discard.begin(), discard.end());
    reverse(deck.begin(), deck.end());
    reverse(shop.begin(), shop.end());
    reverse(drawn.begin(), drawn.end());
    }
  else {
    stable_sort(discard.begin(), discard.end(), f);
    stable_sort(deck.begin(), deck.end(), f);
    stable_sort(shop.begin(), shop.end(), f);
    stable_sort(drawn.begin(), drawn.end(), f);
    }
  check_discard();
  }

void view_game_log() {
  stringstream ss;
  ss << "<div style=\"float:left;width:30%\">&nbsp;</div>";
  ss << "<div style=\"float:left;width:40%\">";
  ss << "<a onclick='back_to_game()'>" << str_back_to_game << "</a><br/><br/>";
  ss << game_log.str();
  ss << "<br/><a onclick='back_to_game()'>" << str_back_to_game << "</a>";
  ss << "</div>";
  ss << "</div>";
  set_value("output", ss.str());
  }

void view_help() {
  stringstream ss;

  ss << "<div style=\"float:left;width:30%\">&nbsp;</div>";
  ss << "<div style=\"float:left;width:40%\">"; 
  ss << "The rules:<br/>";
  ss << "<ul>";
  ss << "<li>Place tiles on the board<ul>";
  ss << "<li>Any word (a complete line of at least two adjacent letters) must be valid</li>";
  ss << "<li>placed tiles must be in a single word (it can include old tiles too)</li>";
  ss << "<li>You score for all the new words you have created</li>";
  ss << "<li>one of the words created must contain an old letter</li></ul></li>";
  ss << "<li>The score for a word is the product of:<ul>";
  ss << "<li>sum of the values of new tiles in the word</li>";
  ss << "<li>sum of the values of all tiles in the word</li>";
  ss << "<li>the multiplier (1 by default)</li></ul></li>";
  ss << "<li>after each move: <ul>";
  ss << "<li>standard copies of the tiles are placed permanently on the board (only geometry-altering and foreign powers are kept)</li>";
  ss << "<li>tiles you have used are discarded</li>";
  ss << "<li>tiles you have not used have their value increased, and are discarded</li>";
  ss << "<li>you draw 8 new tiles from the bag (if bag is empty, discarded tiles go back to the bag), and the shop has a new selection of 6 items</li>";
  ss << "<li>you have to pay a tax which increases in each round</li></ul></li>";
  ss << "<li>tiles bought from the shop can be used immediately or discarded for increased value</li>";
  ss << "<li>the topmost letter in the shop is always A, E, U, I, O</li>";
  ss << "<li>you start with a single standard copy of every letter in your bag; the shop sells letters with extra powers</li>";
  ss << "<li>the board is infinite, but you can only see and use tiles in distance at most 6 from already placed tiles</li>";
  ss << "<li>the multiplier is reduced by 1 for every time when you used the same word in the previous turns</li><ul>";
  ss << "</ul></ul>";

  ss << "<br/><a onclick='back_to_game()'>" << str_back_to_game << "</a><br/>";

  for(auto &s: spells) if(!s.identified) {
    ss << "<br/>" << str_spells_need_identify << "<br/>";
    for(auto& s: spells) if(!spells[s.color_id].identified) ss << "<b>" << s.caption << "</b>: " << s.desc << "<br/>";
    break;
    }
  ss << "<br/>" << str_spells_description << "<br/>";

  ss << "<br/>" << str_tax_shop_price << "<br/>";
  for(int r=1; r<=150; r++) ss << str_turn << " " << r << ": " << str_tax<< " " << taxf(r) << " 🪙 " << str_price << " " << get_min_price(r) << "..." << get_max_price(r) << " 🪙 <br/>";
  ss << "<br/><a onclick='back_to_game()'>" << str_back_to_game << "</a>";
  ss << "</div>"; 
  ss << "</div>"; 

  set_value("output", ss.str());
  }

language *next_language = current;

string dictionary_checked;

void update_dictionary(string s) {
  dictionary_checked = s;
  stringstream ss;
  for(char& c: s) if(c >= 'a' && c <= 'z') c -= 32;
  int len = utf8_length(s);
  if(len < 2) ss << str_least2;
  else {
    map<string, int> in_hand;
    map<string, int> in_shop;
    for(auto& t: drawn) in_hand[t.letter]++;
    for(auto& t: shop) in_shop[t.letter]++;
    int qty = 0;
    for(const string& word: next_language->dictionary[len]) {
      auto in_hand2 = in_hand;
      auto in_shop2 = in_shop;
      int spos = 0, wpos = 0;
      for(int i=0; i<len; i++) {
        string wi = word.substr(wpos, utf8_len(word[wpos])); wpos += utf8_len(word[wpos]);
        string si = s.substr(spos, utf8_len(s[spos])); spos += utf8_len(s[spos]);
        if(wi == si) continue;
        if(si == ".") continue;
        if(si == "$" && in_shop2[wi] > 0) { in_shop2[wi]--; continue; }
        if((si == "?" || si == "$") && in_hand2[wi] > 0) { in_hand2[wi]--; continue; }
        goto next_word;
        }
      qty++;
      if(qty <= 200) {
        ss << word << " ";
        }
      next_word: ;
      }
    if(qty == 0) ss << str_no_matching << " " << s << ".";
    else ss << " (" << qty << " " << str_matching << ")";
    }
  set_value("answer", ss.str());
  }

void view_dictionary() {
  next_language = current;
  stringstream ss;

  ss << "<div style=\"float:left;width:30%\">&nbsp;</div>";
  ss << "<div style=\"float:left;width:40%\">"; 
  ss << str_dict_help << "</br><br/>";

  if(polyglot_languages.size()) {
    for(auto l: languages) {
      add_button(ss, "set_language_dic(\"" + l->name + "\")", l->name + " " + l->flag);
      }
    ss << "<br/><br/>";
    }

  ss << "<input id=\"query\" oninput=\"update_dict(document.getElementById('query').value)\" length=40 type=text/><br/><br/>";
  ss << "<div id=\"answer\"></div>";
  ss << "<br/><a onclick='back_to_game()'>" << str_back_to_game << "</a><br/>";
  ss << "</div></div>"; 

  set_value("output", ss.str());
  }

void review_new_game() {
  stringstream ss;

  ss << "<div style=\"float:left;width:30%\">&nbsp;</div>";
  ss << "<div style=\"float:left;width:40%\">";

  if(game_running) {
    ss << str_last_game << "<br/>";
    ss << str_language << " " << current->name << " " << str_seed << " " << gameseed << "<br/>";
    ss << power_list()  << "<br/>";
    ss << str_turn << ": " << roundindex << " " << str_total_winnings << ": " << total_gain << " 🪙<br/><br/>";
    if(roundindex > 20) ss << str_total_winnings_20 << ": " << total_gain_20 << " 🪙<br/>";
    ss << "<a onclick='back_to_game()'>" << str_back_to_game << "</a><br/><br/>";
    }

  ss << "<br/><br/>";

  auto bak = current;
  current = next_language;

  ss << str_language << " " << next_language->name << "<br/>";

  for(auto l: languages) {
    add_button(ss, "set_language(\"" + l->name + "\")", l->name + " " + l->flag);
    }

  ss << "<br/><br/>";

  ss << str_seed << " <input id=\"seed\" length=10 type=text/><br/>";
  ss << "<br/><br/>";

  ss << str_restricted_specials << " <input id=\"restricted\" length=10 type=text/><br/>";
  ss << str_restrict_example;
  ss << "<br/><br/>";

  string pres;

  ss << str_special_change << "<br>";
  char key = 'a';

  for(auto la: languages) {
    if(la != next_language) {
      string skey; skey += key;
      ss << "<input id=\"" << skey << "\" type=\"checkbox\"/> " << la->name << " " << la->flag << "<br/>";
      pres += "if(document.getElementById(\"" + skey + "\").checked) poly = poly + \"" + skey + "\"; ";
      }
    key++;
    }

  ss << "<br/><input id=\"snaughty\" type=\"checkbox\"/> " << str_naughty << "<br/>";
  pres += "if(document.getElementById(\"snaughty\").checked) poly = poly + \"N\"; ";

  ss << "<br/><br/>";
  add_button(ss, "poly = \"\"; " + pres + "restart(document.getElementById(\"seed\").value, poly, document.getElementById(\"restricted\").value)", "restart");

  ss << "</div></div>";
  set_value("output", ss.str());
  current = bak;
  }

void view_new_game() {
  next_language = current;
  review_new_game();
  }

void new_game();

void set_language(const char *s) {
  for(auto l: languages) if(l->name == s) next_language = l;
  review_new_game();
  }

void set_language_dic(const char *s) {
  for(auto l: languages) if(l->name == s) next_language = l;
  update_dictionary(dictionary_checked);
  }

bool bad_language(sp s) {
  auto lang = get_language(s);
  return lang && !polyglot_languages.count(lang);
  }

void restart(const char *s, const char *poly, const char *_restricted) {
  if(!s[0]) gameseed = time(NULL);
  else gameseed = atoi(s);
  is_daily = false;
  string spoly = poly;
  polyglot_languages = {};
  bool do_naughty = false;
  for(char ch: spoly) {
    if(ch >= 'a' && ch < 'a' + int(languages.size()))
      polyglot_languages.insert(languages[ch - 'a']);
    if(ch == 'D') is_daily = true;
    if(ch == 'N') do_naughty = true;
    }
  for(int i=0; i < (int) sp::first_artifact; i++) {
    special_allowed[i] = (i >= 2) && (do_naughty || i != (int) sp::naughty) && !bad_language(sp(i));
    }
  string restricted = _restricted;
  std::mt19937 restrict_rng(gameseed);
  game_restricted = (restricted != "");

  enabled_spells = true;
  enabled_stay = true;
  enabled_power = true;
  enabled_id = true;

  if(game_restricted) {
    vector<int> chosen;
    restricted += ",";
    string cur;
    for(char c: restricted) {
      if(c == ',') {
        if(cur == "-stay") enabled_stay = false;
        if(cur == "-spell") enabled_spells = false;
        if(cur == "-power") enabled_power = false;
        if(cur == "-id") enabled_id = false;
        for(int i=0; i<(int) sp::first_artifact; i++) if(specials[i].caption->eqcap(cur)) {
          special_allowed[i] = false;
          chosen.push_back(i);
          }
        if(cur.size() && cur[0] == '-') {
          cur = cur.substr(1);
          for(int i=0; i<(int) sp::first_artifact; i++) if(specials[i].caption->eqcap(cur)) {
            special_allowed[i] = false;
            }
          }
        int val = 0;
        for(char digit: cur) if(digit >= '0' && digit <= '9') { val *= 10; val += digit - '0'; }
        for(int i=0; i<val; i++) {
          for(int j=0; j<1000; j++) {
            int r = hrand((int) sp::first_artifact, restrict_rng);
            if(special_allowed[r]) {
              special_allowed[r] = false; chosen.push_back(r); break;
              }
            }
          }
        cur = "";
        }
      else cur += c;
      }
    if(chosen.empty()) {
      game_restricted = false;
      }
    if(game_restricted) {
      for(int i=0; i<(int) sp::first_artifact; i++) special_allowed[i] = false;
      for(int i: chosen) special_allowed[i] = true;
      }
    }
  current = next_language;
  new_game();
  }

}

void draw_tiles(int qty = 8) {
  for(int i=0; i < qty; i++) {
    if(deck.empty()) { swap(deck, discard); scry_active = false; }
    if(deck.empty()) break;
    if(scry_active) {
      drawn.emplace_back(std::move(deck[0]));
      deck.erase(deck.begin());
      }
    else {
      int which = hrand(deck.size(), draw_rng);
      drawn.emplace_back(std::move(deck[which]));
      deck[which] = std::move(deck.back());
      deck.pop_back();
      }
    }
  }

sp basic_special() {
  while(true) {
    int q = hrand(int(sp::first_artifact));
    if(q < 2) continue;
    if(!special_allowed[q]) continue;
    return sp(q);
    }
  }

sp actual_basic_special() {
  while(true) {
    int q = hrand(int(sp::first_artifact));
    if(q < 3) continue;
    if(!special_allowed[q]) continue;
    return sp(q);
    }
  }

sp generate_artifact() {
  int next = int(specials.size());
  specials.emplace_back();
  auto& gs = specials.back();
  string artadj[10] = {"Ancient ", "Embroidered ", "Glowing ", "Shiny ", "Powerful ", "Forgotten ", "Demonic ", "Angelic ", "Great ", "Magical "};
  string artnoun[10] = {"Glyph", "Rune", "Letter", "Symbol", "Character", "Mark", "Figure", "Sign", "Scribble", "Doodle"};
  auto spec = sp(next);
  gs.caption = artadj[hrand_once(10)] + artnoun[hrand_once(10)];
  gs.background = hrand_once(0x1000000); gs.background |= 0xFF000000;
  gs.text_color = 0xFF000000;
  if(!(gs.text_color & 0x800000)) gs.text_color |= 0xFF0000;
  if(!(gs.text_color & 0x008000)) gs.text_color |= 0x00FF00;
  if(!(gs.text_color & 0x000080)) gs.text_color |= 0x0000FF;
  auto& art = artifacts[spec];
  int attempts = 0;
  while(true) {
    art.clear();
    int qty = 3 - (attempts / 100);
    attempts++;
    for(int i=0; i<qty; i++) art.push_back(actual_basic_special());
    bool reps = false;
    for(int i=0; i<qty; i++) for(int j=0; j<i; j++) {
      if(art[i] == art[j]) reps = true;
      if(get_language(art[i]) && get_language(art[j])) reps = true;
      }
    if(reps) continue;
    break;
    }
  return spec;
  }

void build_shop(int qty = 6) {
  for(auto& t: shop) add_to_log("ignored: "+short_desc(t)+ " for " + to_string(t.price));
  shop.clear();
  for(int i=0; i < qty; i++) {
    string l = current->alphabet[hrand(current->alphabet.size())];
    if(shop_id % 6 == 0) l = "AEIOU" [hrand(5)];
    int val = 1;
    int max_price = get_max_price(roundindex);
    int min_price = get_min_price(roundindex);
    int price = min_price + hrand_once(max_price - min_price + 1);
    while(hrand(5) == 0) val++, price += 1 + hrand(roundindex);
    tile t(l, basic_special(), val);
    if(gsp(t).value) {
      int d = hrand(10 * (1 + shop_id / 6));
      if(d >= 200 && hrand(100) < 50) {
        t.special = generate_artifact();
        price *= 6;
        }
      else if(d >= 300) { t.rarity = 3; price *= 10; }
      else if(d >= 100) { t.rarity = 2; price *= 3; }
      }
    t.price = price;
    auto lang = get_language(t);
    if(lang && i) t.letter = lang->alphabet[hrand(lang->alphabet.size())];
    shop.push_back(t);
    shop_id++;
    }
  }

bool under_radiation(coord c) {
  for(int x: {-1,0,1}) for(int y: {-1,0,1}) {
    auto c1 = c + coord(x, y);
    if(board.count(c1) && has_power(board.at(c1), sp::radiating))
      return true;
    }
  return false;
  }

void accept_move() {
  int tax_paid = tax();
  cash += ev.total_score - tax();
  total_gain += ev.total_score;
  if(roundindex <= 20) total_gain_20 = total_gain;
  stacked_mults[roundindex%3] = 0;
  roundindex++;
  int qdraw = 8, qshop = 6, teach = 1, copies_unused = 1, copies_used = 1;
  int retain = 0;

  last_spell_effect = "";
  for(auto& w: ev.new_tricks) old_tricks.insert(w);
  for(auto& w: ev.used_words) word_use_count[w]++;

  stacked_mults[(roundindex + 1)%3] += ev.qdelay;

  for(auto& p: ev.used_tiles) {
    auto& b = board.at(p);
    auto b1 = b; if(get_color(p) == bePower) b1.rarity++;
    auto& sp = gsp(b);
    int val = sp.value * b.rarity;
    if(has_power(b1, sp::rich, val)) qshop += val;
    if(has_power(b1, sp::drawing, val)) qdraw += val;
    if(has_power(b1, sp::teacher, val)) teach += val;
    if(has_power(b1, sp::trasher, val)) copies_unused--;
    if(has_power(b1, sp::multitrasher, val)) copies_unused--;
    if(has_power(b1, sp::duplicator, val)) copies_used += val;
    if(has_power(b1, sp::retain, val)) retain += val;
    if(has_power(b1, sp::wizard, val)) while(val--) {
      int spell_id = hrand_once(spells.size(), spells_rng);
      spells[spell_id].inventory++;
      string out = short_desc(b) + string(str_yields) + spell_desc(spell_id);
      last_spell_effect += out + "<br/>";
      add_to_log(out);
      }
    if(has_power(b1, sp::redrawing, val)) {
      auto& s = spells[spells[0].color_id];
      s.inventory += val; s.identified = true;
      }
    }

  for(auto& p: just_placed) {
    auto& b = board.at(p);
    auto col = get_color(p);
    bool other_end = has_power(b, sp::portal) && p < portals.at(p);
    if(b.price && !other_end) add_to_log("bought: "+short_desc(b)+ " for " + to_string(b.price) + ": " + power_description(b));
    add_to_log("on (" + to_string(p.x) + "," + to_string(p.y) + "): " + short_desc(board.at(p)));
    b.price = 0;
    int selftrash = 0;
    if(has_power(b, sp::trasher)) selftrash = 1;
    if(col == beStay) selftrash++;
    if(has_power(b, sp::portal) && get_color(portals.at(p))) selftrash++;
    if(has_power(b, sp::duplicator)) selftrash++;
    if(!other_end) for(int i=selftrash; i<copies_used; i++) discard.push_back(b);
    bool keep = false;
    for(sp x: {sp::bending, sp::portal, sp::reversing}) if(has_power(b, x)) keep = true;
    if(get_language(b)) keep = true;
    if(col == beStay) keep = true;
    if(col >= beSpell) spells[col - beSpell].inventory++;
    if(!keep) keep = under_radiation(p);
    if(!keep)
      b.special = sp::placed;
    }
  for(auto& p: drawn) {
     if(p.price) add_to_log("bought: "+short_desc(p)+ " for " + to_string(p.price) + power_description(p));
     p.price = 0;
     }
  vector<tile> retained;
  for(auto& p: drawn) {
    if(retain) {
      add_to_log("retaining " + short_desc(p));
      retain--;
      p.value += (teach-1);
      retained.push_back(p);
      continue;
      }
    add_to_log("unused " + short_desc(p));
    p.value += teach;
    for(int c=0; c<copies_unused; c++) discard.push_back(p);
    }

  add_to_log(ev.current_scoring);
  add_to_log("total score: "+to_string(ev.total_score)+" tax: "+to_string(tax_paid)+" cash in round "+to_string(roundindex)+": " + to_string(cash));
  drawn = retained;
  draw_tiles(qdraw);
  just_placed.clear();
  build_shop(qshop);
  draw_board();
  }

void new_game() {
  deck = {};
  old_tricks = {};
  board = {};
  drawn = {};
  shop = {};
  portals = {};
  just_placed = {};
  cash = 80;
  roundindex = 1;
  total_gain = 0;
  for(const string& s: current->alphabet) {
    deck.emplace_back(tile(s, sp::standard));
    }
  int x = 3;
  string title = current->gamename;
  // todo: use UTF8
  for(char c: title) {
    string s0 = ""; s0 += c;
    board.emplace(coord{x++, 7}, tile{s0, sp::placed});
    }

  shop_rng.seed(gameseed);
  board_rng.seed(gameseed);
  draw_rng.seed(gameseed);
  spells_rng.seed(gameseed);
  board_cache.clear();
  colors.clear();
  add_to_log("started SEUPHORICA v18");
  add_to_log(power_list());
  draw_tiles();
  shop_id = 0;
  build_shop();
  colors_swapped = false;
  word_use_count.clear();

  auto g = greek_letters;
  for(int i=0; i<int(spells.size()); i++) {
    auto& s = spells[i];
    s.inventory = 0; s.identified = !enabled_id;
    if(enabled_id) {
      auto& other = spells[hrand_once(1+i, spells_rng)].action_id;
      s.action_id = other; other = i;
      }
    else {
      s.action_id = i;
      }
    int id = hrand_once(g.size(), spells_rng);
    s.greek = g[id]; swap(g[id], g.back()); g.pop_back();
    }
  for(int i=0; i<int(spells.size()); i++) spells[spells[i].action_id].color_id = i;
  last_spell_effect = "";

  for(auto& x: stacked_mults) x = 0;
  identifications = 0;
  scry_active = false;

  draw_board();
  game_running = true;
  }

char secleft[64];

void check_daily_time() {
  time_t t = time(NULL);
  struct tm *res = localtime(&t);
  int seconds_into = res->tm_hour * 3600 + res->tm_min * 60 + res->tm_sec;
  res->tm_hour = 0;
  res->tm_min = 0;
  res->tm_sec = 0;
  time_t t1 = timegm(res);
  daily = t1 / 3600 / 24 - 19843;
  int seconds_left = 3600*24 - seconds_into;
  sprintf(secleft, "%02d:%02d:%02d", seconds_left / 3600, (seconds_left / 60) % 60, seconds_left % 60);
  }

void view_intro() {
  stringstream ss;

  ss << "<div style=\"float:left;width:30%\">&nbsp;</div>";
  ss << "<div style=\"float:left;width:40%\">";

  check_daily_time();

  for(auto l: languages) {
    current = l;
    ss << "<h1>" << l->flag << " " << str_welcome << "</h1>";

    add_button(ss, "set_language(\"" + l->name + "\"); restart(\"\", \"\", \"\");", str_standard_game);
    ss << "<br/><br/>";
    ss << str_exp_standard_game << "<br/><br/>";

    add_button(ss, "set_language(\"" + l->name + "\"); restart(\"" + to_string(daily) + "9\", \"D\", \"8\");", str_daily_game);
    ss << "<br/><br/>";
    ss << str_exp_daily << daily << ", " << str_time_to_next << secleft;
    ss << "<br/><br/>";

    add_button(ss, "set_language(\"" + l->name + "\");", str_custom_game);
    ss << "<br/><br/>";
    ss << str_exp_custom_game;
    ss << "<br/><br/>";
    }

  ss << "</div></div>";
  set_value("output", ss.str());
  }

int init(bool _is_mobile) {
  // next_language = current;
  view_intro();
  // review_new_game();
  return 0;
  }

int dist(coord a, coord b) {
  return max(abs(a.x-b.x), abs(a.y-b.y));
  }

vector<spell> spells = {
  {"Red" + in_pl("Czerwień"), 0xFF2020, "Redraw" + in_pl("Ciąg"), "Redraw the topmost tile." + in_pl("Wymieniasz najwyższą płytkę."), [] {
     if(deck.empty() && discard.empty()) {
       spell_message("You cannot redraw." + in_pl("Nie masz skąd ciągnąć."));
       return;
       }
     draw_tiles(1);
     string str = ("You replace " + short_desc(drawn[0]) + " with " + short_desc(drawn.back()) + ".") + in_pl("Zamieniasz " + short_desc(drawn[0]) + " na " + short_desc(drawn.back()));
     discard.push_back(drawn[0]);
     drawn.erase(drawn.begin());
     spell_message(str);
     }},
  {"Violet" + in_pl("Fiolet"), 0xFF20FF, "Swap" + in_pl("Zamiana"), "Swap red/power and blue/stay symbols." + in_pl("Zamieniasz miejsca czerwone/moc i niebieskie/stój."), [] {
     colors_swapped = !colors_swapped;
     spell_message("You swap the spots on board." + in_pl("Zamieniasz miejsca na planszy."));
    }},
  {"Black" + in_pl("Czerń"), 0x505050, "Trash" + in_pl("Czystość"), "Trash the topmost tile." + in_pl("Wywala najwyższą płytkę."), [] {
    string str = ("You trash " + in_pl("Wyrzuczasz "))->get() + short_desc(drawn[0]) + ".";
    drawn.erase(drawn.begin());
    spell_message(str);
    }},
  {"Green" + in_pl("Zieleń"), 0x20FF20, "Double" + in_pl("Dwa"), "Duplicate the topmost tile." + in_pl("Podwaja najwyższą płytkę."), [] {
    string str = ("You duplicate " + in_pl("Podwajasz "))->get() + short_desc(drawn[0]) + ".";
    drawn.push_back(drawn[0]);
    spell_message(str);
    }},
  {"Golden" + in_pl("Złoto"), 0xFFD500, "Charisma" + in_pl("Charyzma"), "Reduce the shop prices and the topmost tile value to 50% (rounded downwards)." + in_pl("Zmniejsza ceny i wartość najwyższej płytki do połowy (zaokrąglone w dół)."), [] {
    string str = ("You reduce the value of " + short_desc(drawn[0]) + " and shop prices.") + in_pl("Zmniejszasz wartość " + short_desc(drawn[0]) + " i ceny.");
    drawn[0].value = drawn[0].value / 2;
    for(auto& s: shop) s.price = s.price / 2;
    spell_message(str);
    }},
  {"White" + in_pl("Biel"), 0xF0F0F0, "Identify" + in_pl("Wiedza"), "The next spell is identified instead of being used." + in_pl("Kolejny czar jest identyfikowany zamiast używany."), [] {
    identifications++;
    spell_message("You can now identify a spell." + in_pl("Możesz teraz zidentyfikować czar."));
    }},
  {"Blue" + in_pl("Błękit"), 0x4040FF, "Power" + in_pl("Moc"), "Increase the multiplier by 1 for all words this turn." + in_pl("Zwiększa mnożnikw szystkich słów w tej kolejce o 1."), [] {
    stacked_mults[roundindex%3]++;
    spell_message("You gain power. (multiplier +1)" + in_pl("Zdobywasz moc. (mnożnik +1)"));
    }},
  {"Cyan" + in_pl("Turkus"), 0x20FFFF, "Sacrifice" + in_pl("Poświęcenie"), "Decrease the multiplier by 1 for all words this turn, but increase the topmost tile value by 2." + in_pl("Zmniejsza mnożnik o 1 w tej kolejce, ale zwiększa wartość najwyższej płytki o 2."), []{
    string str = ("You sacrifice power but improve " + in_pl("Poświęcasz moc by poprawić: "))->get() + short_desc(drawn[0]) + ".";
    stacked_mults[roundindex%3]--;
    drawn[0].value = drawn[0].value + 2;
    spell_message(str);
    }},
  {"Yellow" + in_pl("Żółć"), 0xFFFF20, "Morph" + in_pl("Morf"), "Change the topmost tile to the next letter in the alphabet." + in_pl("Zmienia najwyższą płytkę na kolejną literę w alfabecie."), []{
    string old = short_desc(drawn[0]);
    drawn[0].letter = alphashift(drawn[0], 1);
    string str = ("You morph " + old + " to " + short_desc(drawn[0]) + ".") + in_pl("Przekształcasz: " + old + " -> " + short_desc(drawn[0]));
    spell_message(str);
    }},
  {"Brown" + in_pl("Brąz"), 0x804000, "Scry" + in_pl("Wróżba"), "See the order of tiles in your bag." + in_pl("Pokazuje kolejność płytek w worku."), [] {
    if(!scry_active) {
      scry_active = true;
      vector<tile> new_deck;
      while(deck.size()) {
        int which = hrand(deck.size(), draw_rng);
        new_deck.emplace_back(std::move(deck[which]));
        deck[which] = std::move(deck.back());
        deck.pop_back();
        }
      deck = std::move(new_deck);
      }
    string str = "Scrying: " + in_pl("Widzisz: ");
    int q = 0;
    for(auto& b: deck) {
      if(q) str += ", ";
      str += short_desc(b);
      q++;
      }
    spell_message(str);
    scry_active = true;
    }},
  };

void spell_message(const string& s) {
  last_spell_effect = s;
  add_to_log(s);
  draw_board();
  }

extern "C" {
  void start(bool mobile) { gameseed = time(NULL); init(mobile); }
  
  void back_from_board(int x, int y);

  void drop_hand_on(int x, int y) {
    coord c(x, y);
    if(placing_portal) {
      auto t = board.at(portal_from);
      int d = dist(portal_from, c);
      int val;
      bool powered = get_color(portal_from) == bePower && get_color(c) == bePower;
      if(powered) t.rarity++;
      has_power(t, sp::portal, val);
      if(powered) t.rarity--;
      if(d > val) { placing_portal = false; back_from_board(portal_from.x, portal_from.y); return; }
      board.emplace(c, t);
      portals.emplace(c, portal_from);
      portals.emplace(portal_from, c);
      just_placed.insert(c);
      placing_portal = false;
      draw_board();
      return;
      }
    if(drawn.size()) {
       board.emplace(c, std::move(drawn[0])); just_placed.insert(c); drawn.erase(drawn.begin());
       if(has_power(board.at(c), sp::portal)) { placing_portal = true; portal_from = c; }
       draw_board();
       }
    }

  void back_to_drawn() {
    }

  void draw_to_hand(int i) {
    if(!drawn.size()) return;
    auto hand = std::move(drawn[i]); 
    drawn.erase(drawn.begin() + i);
    drawn.insert(drawn.begin(), hand);
    draw_board();
    }

  void back_from_board(int x, int y) {
    coord c(x, y);
    if(!just_placed.count(c)) return;
    if(!board.count(c)) return;
    if(has_power(board.at(c), sp::portal) && !portals.count(c)) placing_portal = false;
    drawn.insert(drawn.begin(), board.at(c));
    board.erase(c); just_placed.erase(c);
    if(portals.count(c)) { auto c1 = portals.at(c); portals.erase(c); portals.erase(c1); board.erase(c1); just_placed.erase(c1); }
    draw_board();
    }

  void buy(int i) {
    if(cash >= shop[i].price) {
      cash -= shop[i].price;
      drawn.insert(drawn.begin(), std::move(shop[i])); shop.erase(shop.begin() + i); draw_board();
      }
    }

  void cast_spell(int i) {
    if(!spells[i].identified && identifications) {
      spells[i].identified = true; identifications--;
      spell_message(str_cast_identify->get() + spell_desc(i) + ".");
      return;
      }
    if(!spells[i].inventory) {
      string g = spells[spells[i].action_id].caption->get();
      spell_message(str_cast_zero->get() + g + ".");
      return;
      }
    if(drawn.empty()) {
      spell_message(str_cast_emptyhand);
      return;
      }
    spells[i].inventory--; spells[i].identified = true;
    spells[spells[i].action_id].action();
    }

  void back_to_shop() {
    if(drawn.size() && drawn[0].price) { cash += drawn[0].price; shop.push_back(drawn[0]); drawn.erase(drawn.begin()); draw_board(); }
    }

  void back_to_game() { draw_board(); }

  void wild_become(int id, const char *s) {
    if(drawn.size() > id && has_power(drawn[id], sp::wild)) { drawn[id].letter = s; drawn[id].value = 0; } draw_board();
    }

  void play() {
    if(ev.valid_move) accept_move();
    }

  void cheat() {
    cash += 1000000;
    for(auto t: discard) drawn.push_back(t);
    for(auto t: deck) drawn.push_back(t);
    build_shop(100);
    discard.clear(); deck.clear(); draw_board();
    }

  void update_dict(const char* s) { update_dictionary(s); }
  }
                                  