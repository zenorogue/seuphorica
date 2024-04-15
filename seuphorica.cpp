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

const int lsize = 40;

int next_id;

struct special {
  string caption;
  string desc;
  int value;
  unsigned background;
  unsigned text_color;
  };

enum class language_state { not_fetched, fetch_started, fetch_progress, fetch_success, fetch_fail };

struct language {
  language_state state;
  map<int, set<string>> dictionary;
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
language *current = &english;

vector<language*> languages = {&english, &polski};

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

bool ok(const string& word, int len) {
  return current->dictionary[len].count(word);
  }

vector<special> specials = {
  {"No Tile", "", 0, 0xFF000000, 0xFF000000}, 
  {"Placed", "", 0, 0xFFFFFFFF, 0xFF000000},   
  {"Standard", "no special properties", 0, 0xFFFFFF80, 0xFF000000}, 

  /* premies */

  {"Premium", "%+d multiplier when used", 1, 0xFFFF8080, 0xFF000000},
  {"Horizontal", "%+d multiplier when used horizontally", 2, 0xFFFF80C0, 0xFF000000},
  {"Vertical", "%+d multiplier when used vertically", 2, 0xFFFF80C0, 0xFF000000},
  {"Initial", "%+d multiplier when this is the first letter of the word", 3, 0xFFFFFFFF, 0xFF808080},
  {"Final", "%+d multiplier when this is the last letter of the word", 3, 0xFFFFFFFF, 0xFF808080},
  {"Red", "%+d multiplier when put on a red square", 4, 0xFFFF2020, 0xFFFFFFFF},
  {"Blue", "%+d multiplier when put on a blue square", 3, 0xFF2020FF, 0xFFFFFFFF},

  /* placement */
  {"Flying", "exempt from the rule that all tiles must be in a single word", 0, 0xFF8080FF, 0xFFFFFFFF},
  {"Mirror", "words going across go down after this letter, and vice versa; %+d multiplier when tiles on all 4 adjacent cells", 3, 0xFF303030, 0xFF4040FF},
  {"Reversing", "words are accepted when written in reverse; if both valid, both score", 0, 0xFF404000, 0xFFFFFF80},

  /* unused-tile effects */
  {"Teacher", "if used, %+d value to all the unused tiles", 1, 0xFFFF40FF, 0xFF000000},
  {"Trasher", "all discarded unused tiles, as well as this, are premanently deleted", 0, 0xFF000000, 0xFF808080},
  {"Trasher+", "all discarded unused tiles are premanently deleted", 0, 0xFF000000, 0xFFFFFFFF},
  {"Duplicator", "%+d copies of all used tiles (but this one is deleted)", 1, 0xFFFF40FF, 0xFF00C000},
  {"Retain", "%d first unused tiles are retained for the next turn", 2, 0xFF905000, 0xFFFFFFFF},

  /* next-turn effects */
  {"Drawing", "%+d draw in the next round", 2, 0xFFFFC080, 0xFF000000},
  {"Rich", "%+d shop choices in the next round", 5, 0xFFFFE500, 0xFF800000},

  /* other */
  {"Radiating", "8 adjacent tiles keep their special properties", 0, 0xFF004000, 0xFF80FF80},
  {"Tricky", "all valid subwords including this letter are taken into account for scoring (each counting just once)", 0, 0xFF808040, 0xFFFFFF80},
  {"Soothing", "every failed multiplier tile becomes %+d multiplier (does not stack)", 1, 0xFF80FF80, 0xFF008000},
  {"Wild", "you can rewrite the letter while it is in your hand, but it resets the value to 0", 0, 0xFF800000, 0xFFFF8000},
  {"Portal", "placed in two locations; teleports between them (max distance %d)", 6, 0xFF000080, 0xFFFFFFFF},
  };

enum class sp {
  notile, placed, standard,

  premium, horizontal, vertical, initial, final, red, blue, 
  flying, bending, reversing,
  teacher, trasher, multitrasher, duplicator, retain,
  drawing, rich,
  radiating, tricky, soothing, wild, portal, first_artifact
  };

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

set<coord> just_placed;

map<coord, tile> board;

map<coord, int> colors;

set<vector<coord>> old_tricks;

map<coord, coord> portals;

bool placing_portal;
coord portal_from(0, 0);

int get_color(coord c) {
  if(!colors.count(c)) { 
    int r = rand() % 25;
    if(r == 24) colors[c] = 1;
    else if(r == 22 || r == 23) colors[c] = 2;
    else colors[c] = 0;
    }
  return colors[c];
  }

vector<tile> drawn;
vector<tile> deck;
vector<tile> discard;
vector<tile> shop;

tile empty_tile(0, sp::notile);

special &gsp(sp x) {
  return specials[int(x)];
  }

special &gsp(const tile &t) {
  return gsp(t.special);
  }

string power_description(const special& s, int rarity) {
  char buf[127];
  sprintf(buf, s.desc.c_str(), s.value * rarity);
  return buf;
  }

string power_description(const tile &t) {
  if(t.special >= sp::first_artifact) {
    stringstream ss;
    int qty = 0;
    for(auto p: artifacts[t.special]) {
      if(qty) ss << "; "; qty++;
      ss << power_description(gsp(p), 1);
      }
    return ss.str();
    }
  return power_description(gsp(t), t.rarity);
  }

string short_desc(const tile& t) {
  auto& s = gsp(t);
  string out = s.caption;
  if(t.rarity == 2) out = "Rare " + out;
  if(t.rarity == 3) out = "Epic " + out;
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
  if(t.special >= sp::first_artifact)
    out = "<b><font color='#FFD500'>" + cap + ": </font></b>";
  else if(t.rarity == 1)
    out = "<b>" + cap + ": </b>";
  else if(t.rarity == 2)
    out = "<b><font color='#4040FF'>Rare " + cap + ": </font></b>";
  else if(t.rarity == 3)
    out = "<b><font color='#FF40FF'>Epic " + cap + ": </font></b>";
  out += power_description(t);
  if(t.price) out += " (" + to_string(t.price) + " 🪙)";
  return out;
  }

bool has_power(const tile& t, sp which, int& val) {
  auto& s = gsp(t);
  if(t.special == which) { val = s.value * t.rarity; return true; }
  if(t.special >= sp::first_artifact) {
    const auto& artifact = artifacts[t.special];
    for(auto w: artifact) if(w == which) { val = gsp(w).value; return true; }
    }
  return false;
  }

bool has_power(const tile& t, sp which) {
  int dummy; return has_power(t, which, dummy);
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

int cash = 80;

int roundindex = 1;
int total_gain = 0;

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

struct eval {
  int total_score;
  string current_scoring;
  bool valid_move;
  set<coord> used_tiles;
  set<vector<coord>> new_tricks;
  };

eval ev;

void compute_score() {

  if(current->state == language_state::not_fetched) {
    read_dictionary(*current);
    }
  if(current->state == language_state::fetch_started) {
    ev.current_scoring = "Downloading the dictionary...";
    ev.valid_move = false;
    return;
    }
  if(current->state == language_state::fetch_progress) {
    ev.current_scoring = "Downloading the dictionary... " + to_string(current->offset) + " of " + to_string(current->bytes) + " B";
    ev.valid_move = false;
    return;
    }

  if(current->state == language_state::fetch_fail) {
    ev.current_scoring = "Failed to download the dictionary!";
    ev.valid_move = false;
    return;
    }

  if(placing_portal) { ev.current_scoring = "You must finish placing the portal";  ev.valid_move = false; return; }

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
          steps++; if(steps >= 10000) { ev.current_scoring = "Cannot create infinite words"; ev.valid_move = false; return; }
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
    int placed = 0, all = 0, mul = 1;
    string word;
    set<coord> needed;
    for(auto p: just_placed) if(!has_power(board.at(p), sp::flying)) needed.insert(p);
    int index = 0;
    bool has_tricky = false;
    bool has_reverse = false;
    bool optional = board.count(at-next);
    vector<coord> allword;
    int rmul = 1;
    int qsooth = 0;
    int sooth = 0, rsooth = 0;
    string rword;

    while(board.count(at)) {
      needed.erase(at);
      ev.used_tiles.insert(at);
      auto& b = board.at(at);
      word += b.letter;
      rword = b.letter + rword;
      allword.push_back(at);

      if(just_placed.count(at)) placed += b.value;
      else is_crossing = true;

      all += b.value;
      int val = gsp(b).value * b.rarity;

      auto affect_mul = [&] (bool b, int ways = 3) {
        if(b) { if(ways & 1) mul += val; if(ways & 2) rmul += val; }
        if(!b) { if(ways & 1) sooth++; if(ways & 2) rsooth++; }
        };

      if(has_power(b, sp::tricky, val)) has_tricky = true;
      if(has_power(b, sp::soothing, val)) qsooth = max(qsooth, val);
      if(has_power(b, sp::reversing, val)) has_reverse = true;
      if(has_power(b, sp::bending, val)) next = next.mirror();
      if(has_power(b, sp::portal, val)) { at = portals.at(at); ev.used_tiles.insert(at); needed.erase(at); }
      if(has_power(b, sp::premium, val)) affect_mul(true);
      if(has_power(b, sp::horizontal, val)) affect_mul(next.x);
      if(has_power(b, sp::vertical, val)) affect_mul(next.y);
      if(has_power(b, sp::red, val)) affect_mul(get_color(at) == 1);
      if(has_power(b, sp::blue, val)) affect_mul(get_color(at) == 2);
      if(has_power(b, sp::initial, val)) { affect_mul(index == 0, 1); }
      if(has_power(b, sp::final, val)) { affect_mul(index == 0, 2); }
      if(has_power(b, sp::bending, val))
        affect_mul(board.count(at-coord(1,0)) && board.count(at+coord(1,0)) && board.count(at-coord(0,1)) && board.count(at+coord(0,1)));
      at = at + next;
      if(has_power(b, sp::final, val)) affect_mul(!board.count(at), 1);
      if(has_power(b, sp::initial, val)) affect_mul(!board.count(at), 2);
      index++;
      if(has_tricky && ok(word, index) && board.count(at) && !old_tricks.count(allword)) {
        int mul1 = mul + qsooth * sooth;
        scoring << "<b>" << word << ":</b> " << placed << "*" << all << "*" << mul1 << " = " << placed*all*mul1 << "<br/>";
        ev.total_score += placed * all * mul1;
        ev.new_tricks.insert(allword);
        }
      if(has_tricky && has_reverse && ok(rword, index) && board.count(at) && !old_tricks.count(allword)) {
        int mul1 = rmul + qsooth * rsooth;
        scoring << "<b>" << rword << ":</b> " << placed << "*" << all << "*" << mul1 << " = " << placed*all*mul1 << "<br/>";
        ev.total_score += placed * all * mul1;
        ev.new_tricks.insert(allword);
        }
      }
    if(needed.empty()) ev.valid_move = true;
    bool is_legal = ok(word, index);
    if(!is_legal && has_reverse && ok(rword, index)) {
      has_reverse = false; is_legal = true; swap(mul, rmul); swap(sooth, rsooth); swap(word, rword);
      }
    if(!is_legal && optional) continue;
    int mul1 = mul + qsooth * sooth;
    scoring << "<b>" << word << ":</b> " << placed << "*" << all << "*" << mul1 << " = " << placed*all*mul1;
    if(!is_legal) { scoring << " <font color='#FF4040'>(illegal word!)</font>"; illegal_words = true; }
    scoring << "<br/>";
    ev.total_score += placed * all * mul;

    if(is_legal && has_reverse && ok(rword, index)) {
      swap(mul, rmul); swap(sooth, rsooth); swap(word, rword);
      mul1 = mul + qsooth * sooth;
      scoring << "<b>" << word << ":</b> " << placed << "*" << all << "*" << mul1 << " = " << placed*all*mul1 << "<br/>";
      ev.total_score += placed * all * mul1;
      }
    }

  if(just_placed.empty()) { scoring << "You can skip your move, but you still need to pay the tax of " << tax() << " 🪙."; }
  else if(illegal_words) { scoring << "Includes words not in the dictionary!"; ev.valid_move = false; }
  else if(fly_away) { scoring << "Single flying letters cannot just fly away!"; ev.valid_move = false; }
  else if(!is_crossing) { scoring << "Must cross the existing letters!"; ev.valid_move = false; }
  else if(!ev.valid_move) scoring << "All placed letters must be a part of a single word!";
  else scoring << "Total score: " << ev.total_score << " 🪙 Tax: " << tax() << " 🪙";

  if(cash + ev.total_score < tax()) { scoring << "<br/>You do not score enough to pay the tax!"; ev.valid_move = false; }

  ev.current_scoring = scoring.str();
  };

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
    if(c) {
      style bred(0, 0xFFFF0000, 0);
      style bblue(0, 0xFF0000FF, 0);
      path pa(c == 1 ? bred : bblue);
      pa.add(vec(x*lsize+lsize/2, y*lsize+lsize/4));
      pa.add(vec(x*lsize+lsize/4, y*lsize+lsize/2));
      pa.add(vec(x*lsize+lsize/2, y*lsize+lsize*3/4));
      pa.add(vec(x*lsize+lsize*3/4, y*lsize+lsize/2));
      pa.onclick = s;
      pa.cycled = true;
      p += pa;
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

  ss << "<b>Tiles in hand: </b>(<a onclick='check_discard()'>discard: " << discard.size() << " bag: " << deck.size() << "</a>)<br/>";

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
  ss << "</div>";

  ss << "<div style=\"float:left;width:20%\">";
  ss << "<b>Shop: (you have " << cash << " 🪙)</b><br/>";

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
    ss << SVG_to_string(p) << " cancel the purchase<br/>";
    }
  ss << "</div>";
  ss << "<div style=\"float:left;width:20%\">";
  ss << "Turn: " << roundindex << " total winnings: " << total_gain << " 🪙<br/>";
  ss << ev.current_scoring << "<br/><br/>";
  if(ev.valid_move && just_placed.empty()) {
    add_button(ss, "play()", "skip turn!");
    ss << "<br/>";
    }
  else if(ev.valid_move) {
    add_button(ss, "play()", "play!");
    ss << "<br/>";
    }
  ss << "<hr/>";
  ss << "<a onclick='view_help()'>view help</a>";
  ss << " - <a onclick='view_dictionary()'>dictionary</a>";
  ss << " - <a onclick='view_game_log()'>view game log</a>";
  ss << "<br/><a onclick='view_new_game()'>start new game ";
  for(auto& l: languages) ss << l->flag;
  ss << "</a>";
  ss << "</div></div>";
  ss << "</div>";

  set_value("output", ss.str());
  }

extern "C" {

void check_discard() {
  stringstream ss;

  ss << "<div style=\"float:left;width:10%\">&nbsp;</div>";
  ss << "<div style=\"float:left;width:40%\">"; 
  ss << "<b>Discard:</b><br/>";
  for(auto& t: discard) {
    pic p;
    render_tile(p, 0, 0, t, "");
    ss << SVG_to_string(p) << " " << tile_desc(t) << " <br/>";
    }
  ss << "<a onclick='back_to_game()'>back to game</a> - sort by ";
  ss << "<a onclick='sort_by(1)'>letter</a>";
  ss << " / <a onclick='sort_by(2)'>value</a>";
  ss << " / <a onclick='sort_by(3)'>special</a>";
  ss << " / <a onclick='sort_by(4)'>rarity</a>";
  ss << " / <a onclick='sort_by(5)'>reverse</a>";
  ss << "</div>";
  ss << "<div style=\"float:left;width:40%\">"; 
  ss << "<b>Bag:</b><br/>";
  for(auto& t: deck) {
    pic p;
    render_tile(p, 0, 0, t, "");
    ss << SVG_to_string(p) << " " << tile_desc(t) << " <br/>";
    }
  ss << "<a onclick='back_to_game()'>back to game</a>";
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
  ss << "<a onclick='back_to_game()'>back to game</a><br/><br/>";
  ss << game_log.str();
  ss << "<br/><a onclick='back_to_game()'>back to game</a>";
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
  ss << "<li>standard copies of the tiles are placed permanently on the board (only geometry-altering powers are kept)</li>";
  ss << "<li>tiles you have used are discarded</li>";
  ss << "<li>tiles you have not used have their value increased, and are discarded</li>";
  ss << "<li>you draw 8 new tiles from the bag (if bag is empty, discarded tiles go back to the bag), and the shop has a new selection of 5 items</li>";
  ss << "<li>you have to pay a tax which increases in each round</li></ul></li>";
  ss << "<li>tiles bought from the shop can be used immediately or discarded for increased value</li>";
  ss << "<li>the topmost letter in the shop is always A, E, U, I, O</li>";
  ss << "<li>you start with a single standard copy of every letter in your bag; the shop sells letters with extra powers</li>";
  ss << "<li>the board is infinite, but you can only see and use tiles in distance at most 6 from already placed tiles<ul>";
  ss << "</ul>";

  ss << "<br/><a onclick='back_to_game()'>back to game</a><br/>";

  ss << "<br/>Tax and shop price:<br/>";
  for(int r=1; r<=150; r++) ss << "Round " << r << ": tax " << taxf(r) << " 🪙 price: " << get_min_price(r) << "..." << get_max_price(r) << " 🪙 <br/>";
  ss << "<br/><a onclick='back_to_game()'>back to game</a>";
  ss << "</div>"; 
  ss << "</div>"; 

  set_value("output", ss.str());
  }

void update_dictionary(string s) {
  stringstream ss;
  for(char& c: s) if(c >= 'a' && c <= 'z') c -= 32;
  int len = utf8_length(s);
  if(len < 2) ss << "Enter at least 2 letters!";
  else {
    map<string, int> in_hand;
    map<string, int> in_shop;
    for(auto& t: drawn) in_hand[t.letter]++;
    for(auto& t: shop) in_shop[t.letter]++;
    int qty = 0;
    for(const string& word: current->dictionary[len]) {
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
    if(qty == 0) ss << "No matching words: " << s << ".";
    else ss << " (" << qty << " matching words)";
    }
  set_value("answer", ss.str());
  }

void view_dictionary() {
  stringstream ss;

  ss << "<div style=\"float:left;width:30%\">&nbsp;</div>";
  ss << "<div style=\"float:left;width:40%\">"; 
  ss << "Enter the word to check whether it is in the dictionary.</br>";
  ss << "You can use: '.' for any letter, '?' for any letter in hand, '$' for any letter in hand or shop.</br><br/>";
  ss << "<input id=\"query\" oninput=\"update_dict(document.getElementById('query').value)\" length=40 type=text/><br/><br/>";
  ss << "<div id=\"answer\"></div>";
  ss << "<br/><a onclick='back_to_game()'>back to game</a><br/>";
  ss << "</div></div>"; 

  set_value("output", ss.str());
  }

language *next_language = current;

void view_new_game() {
  stringstream ss;

  ss << "<div style=\"float:left;width:30%\">&nbsp;</div>";
  ss << "<div style=\"float:left;width:40%\">";

  if(roundindex > 1) {
    ss << "Your last game:<br/>";
    ss << "Turn: " << roundindex << " total winnings: " << total_gain << " 🪙<br/><br/>";
    }

  ss << "<br/><br/>";

  ss << "chosen language: " << next_language->name << "<br/>";

  for(auto l: languages) {
    add_button(ss, "set_language(\"" + l->name + "\")", l->name + " " + l->flag);
    }

  ss << "<br/><br/>";
  add_button(ss, "restart()", "restart");

  ss << "</div></div>";
  set_value("output", ss.str());
  }

int init(bool _is_mobile);

void set_language(const char *s) {
  for(auto l: languages) if(l->name == s) next_language = l;
  view_new_game();
  }

void restart() {
  current = next_language;
  init(false);
  }

}

void draw_tiles(int qty = 8) {
  for(int i=0; i < qty; i++) {
    if(deck.empty()) swap(deck, discard);
    if(deck.empty()) break;
    int which = rand() % deck.size();
    drawn.emplace_back(std::move(deck[which]));
    deck[which] = std::move(deck.back());
    deck.pop_back();
    }
  }

sp basic_special() {
  int q = 0;
  while(q < 2) {
    q = rand() % int(sp::first_artifact);
    }
  return sp(q);
  }

sp actual_basic_special() {
  int q = 0;
  while(q < 3) {
    q = rand() % int(sp::first_artifact);
    }
  return sp(q);
  }

sp generate_artifact() {
  int next = int(specials.size());
  specials.emplace_back();
  auto& gs = specials.back();
  string artadj[10] = {"Ancient ", "Embroidered ", "Glowing ", "Shiny ", "Powerful ", "Forgotten ", "Demonic ", "Angelic ", "Great ", "Magical "};
  string artnoun[10] = {"Glyph", "Rune", "Letter", "Symbol", "Character", "Mark", "Figure", "Sign", "Scribble", "Doodle"};
  auto spec = sp(next);
  gs.caption = artadj[rand() % 10] + artnoun[rand() % 10];
  gs.background = rand() % 0x1000000; gs.background |= 0xFF000000;
  gs.text_color = 0xFF000000;
  if(!(gs.text_color & 0x800000)) gs.text_color |= 0xFF0000;
  if(!(gs.text_color & 0x008000)) gs.text_color |= 0x00FF00;
  if(!(gs.text_color & 0x000080)) gs.text_color |= 0x0000FF;
  auto& art = artifacts[spec];
  while(true) {
    art.clear();
    for(int i=0; i<3; i++) art.push_back(actual_basic_special());
    bool reps = false;
    for(int i=0; i<3; i++) for(int j=0; j<i; j++) if(art[i] == art[j]) reps = true;
    if(reps) continue;
    break;
    }
  return spec;
  }

void build_shop(int qty = 6) {
  for(auto& t: shop) add_to_log("ignored: "+short_desc(t)+ " for " + to_string(t.price));
  shop.clear();
  for(int i=0; i < qty; i++) {
    string l = current->alphabet[rand() % current->alphabet.size()];
    if(i == 0) l = "AEIOU" [rand() % 5];
    int val = 1;
    int max_price = get_max_price(roundindex);
    int min_price = get_min_price(roundindex);
    int price = min_price + rand() % (max_price - min_price + 1);
    while(rand() % 5 == 0) val++, price += 1 + rand() % roundindex;
    tile t(l, basic_special(), val);
    if(gsp(t).value) {
      int d = rand() % (10 * roundindex);
      if(d >= 200 && rand() % 100 < 50) {
        t.special = generate_artifact();
        price *= 6;
        }
      else if(d >= 300) { t.rarity = 3; price *= 10; }
      else if(d >= 100) { t.rarity = 2; price *= 3; }
      }
    t.price = price;
    shop.push_back(t);
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
  roundindex++;
  int qdraw = 8, qshop = 6, teach = 1, copies_unused = 1, copies_used = 1;
  int retain = 0;

  for(auto& w: ev.new_tricks) old_tricks.insert(w);

  for(auto& p: ev.used_tiles) {
    auto& b = board.at(p);
    auto& sp = gsp(b);
    int val = sp.value * b.rarity;
    if(has_power(b, sp::rich, val)) qshop += val;
    if(has_power(b, sp::drawing, val)) qdraw += val;
    if(has_power(b, sp::teacher, val)) teach += val;
    if(has_power(b, sp::trasher, val)) copies_unused--;
    if(has_power(b, sp::multitrasher, val)) copies_unused--;
    if(has_power(b, sp::duplicator, val)) copies_used += val;
    if(has_power(b, sp::retain, val)) retain += val;
    }

  for(auto& p: just_placed) {
    auto& b = board.at(p);
    bool other_end = has_power(b, sp::portal) && p < portals.at(p);
    if(b.price && !other_end) add_to_log("bought: "+short_desc(b)+ " for " + to_string(b.price) + ": " + power_description(b));
    add_to_log("on (" + to_string(p.x) + "," + to_string(p.y) + "): " + short_desc(board.at(p)));
    b.price = 0;
    int selftrash = 0;
    if(has_power(b, sp::trasher)) selftrash = 1;
    if(has_power(b, sp::duplicator, selftrash)) selftrash++;
    if(!other_end) for(int i=selftrash; i<copies_used; i++) discard.push_back(b);
    bool keep = false;
    for(sp x: {sp::bending, sp::portal, sp::reversing}) if(has_power(b, x)) keep = true;
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
      retained.push_back(p);
      p.value += (teach-1);
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

int init(bool _is_mobile) {
  deck = {};
  board = {};
  drawn = {};
  shop = {};
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
  srand(time(NULL));
  add_to_log("started SEUPHORICA");
  draw_tiles();
  build_shop();
  draw_board();
  return 0;
  }

int dist(coord a, coord b) {
  return max(abs(a.x-b.x), abs(a.y-b.y));
  }

extern "C" {
  void start(bool mobile) { init(mobile); }
  
  void back_from_board(int x, int y);

  void drop_hand_on(int x, int y) {
    coord c(x, y);
    if(placing_portal) {
      auto t = board.at(portal_from);
      int d = dist(portal_from, c);
      int val;
      has_power(t, sp::portal, val);
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
                                  