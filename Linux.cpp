#include <iostream>
#include <chrono>
#include <thread>
#include <sstream>
#include <deque>
#include <array>
#include <stdexcept>
#include <cstdio>
#include <cstdlib>
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>
#include <csignal>
#include <sys/ioctl.h>
#include <algorithm>
#include <future>
#include <mutex>
#include <locale>
#include <cmath>
#include <climits>
#include <vector>

using namespace std;

int W = 80, H = 25;
int MAX_rainS = 150, MIN_GAP = 1, FRAME_MS = 30;
double DENSITY = 0.6, BASE_SPEED = 2.5, SPEED_VAR = 0.6, FADE = 0.1, FLICKER = 0.2, BIAS = 0.5;
int MAX_rain_LENGTH = 20, rain_LENGTH_VAR = 5;
double HEAD_CHAR_FREQ = 0.33;

constexpr array<const char*, 5> rain_COLORS = {
    "\x1b[38;5;255m",
    "\x1b[38;5;40m",
    "\x1b[38;5;34m",
    "\x1b[38;5;28m",
    "\x1b[38;5;22m"
};
constexpr const char* BACKGROUND = "\x1b[48;5;232m";
constexpr const char* RESET = "\x1b[0m";

string chars_basic = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567&*()_+=-`~[]\\{}|;':\",./<>?";
string chars_katakana = "ｱｲｳｴｵｶｷｸｹｺｻｼｽｾｿﾀﾁﾂﾃﾄﾅﾆﾇﾈﾉﾊﾋﾌﾍﾎﾏﾐﾑﾒﾓﾔﾕﾖﾗﾘﾙﾚﾛﾜｦﾝ";
string chars = chars_basic + chars_katakana;

unsigned int rng_seed = chrono::steady_clock::now().time_since_epoch().count();
unsigned int xorshift() {
    rng_seed ^= rng_seed << 13;
    rng_seed ^= rng_seed >> 17;
    rng_seed ^= rng_seed << 5;
    return rng_seed;
}
double rng_double()
{
    return (double)xorshift() / (double)UINT_MAX;
}

int rng_int(int min, int max)
{
    return (xorshift() % (max - min + 1)) + min;
}

char getChar(bool head = false, const string& charSet = "") {
    const string& useChars = (charSet == "basic") ? chars_basic :
        (charSet == "katakana") ? chars_katakana : chars;
    int dist = rng_int(0, useChars.length() - 1);
    if (head && (rng_double() < HEAD_CHAR_FREQ)) {
        int headDist = rng_int(0, chars_basic.length() - 1);
        return chars_basic[headDist];
    }
    return useChars[dist];
}

struct SegmentType {
    string charSet;
    string color;
    SegmentType(string charSet, string color) : charSet(charSet), color(color) {}
};

SegmentType SEGMENT_TYPES[] = {
  {"basic", string(rain_COLORS[0])},
  {"basic", string(rain_COLORS[0])},
  {"basic", string(rain_COLORS[0])},
  {"basic", string(rain_COLORS[1])},
  {"basic", string(rain_COLORS[1])},
  {"basic", string(rain_COLORS[2])},
  {"basic", string(rain_COLORS[2])},
   {"basic", string(rain_COLORS[3])},
  {"basic", string(rain_COLORS[3])},
   {"basic", string(rain_COLORS[4])},
  {"basic", string(rain_COLORS[4])}
};


struct Segment { char c; double a; int segmentType; };
struct rain { int x; double y, s; deque<Segment> segs; int maxLen; };

void update(rain& t) {
    t.y += t.s * (1 + BIAS);
    int segmentType = 0;
    t.segs.push_front({ getChar(true, SEGMENT_TYPES[segmentType].charSet), 1.0, segmentType });
    if (t.segs.size() > t.maxLen) t.segs.pop_back();

    for (size_t i = 1; i < t.segs.size(); ++i) {
        if (i < sizeof(SEGMENT_TYPES) / sizeof(SEGMENT_TYPES[0]))
        {
            t.segs[i].segmentType = i;
        }
        else
        {
            t.segs[i].segmentType = sizeof(SEGMENT_TYPES) / sizeof(SEGMENT_TYPES[0]) - 1;
        }
        t.segs[i].a = (rng_double() < FLICKER) ? (t.segs[i].a > 0 ? 0 : 1.0) : max(0.0, t.segs[i].a - FADE);
    }
    if (t.y > H + t.segs.size()) t.y = 0;
}

mutex screen_chars_mutex;

string render(const deque<rain>& rains, const string& commandLine, bool showHelp, bool showList, const string& listText) {
    string screen(W * H, ' ');
    stringstream ss;
    deque<tuple<int, char, string>> screen_chars;

    for (const auto& t : rains) {
        int y = floor(t.y);
        for (size_t i = 0; i < t.segs.size(); ++i) {
            double renderY = t.y - i;
            if (renderY < H && renderY >= 0 && t.segs[i].a > 0) {
                int index = floor(renderY) * W + t.x;
                if (index >= 0 && index < W * H) {
                    {
                       lock_guard<mutex> lock(screen_chars_mutex);
                       screen_chars.emplace_back(index, t.segs[i].c, SEGMENT_TYPES[t.segs[i].segmentType].color);
                       screen[index] = t.segs[i].c;
                    }
                }
            }
        }
    }

    ss << BACKGROUND;
    for (int i = 0; i < H; i++) {
        for (int j = 0; j < W; j++) {
            int index = i * W + j;
            bool found = false;
            for (const auto& [pos, c, color] : screen_chars) {
                if (pos == index) {
                    ss << color << c << RESET;
                    found = true;
                    break;
                }
            }
            if (!found) {
                ss << " ";
            }
        }
        ss << endl;
    }

    int commandLineY = H - 1;
    string prompt = "> " + commandLine;
    ss << "\x1b[" << commandLineY + 1 << ";1H" << RESET << prompt << string(max(0, W - (int)prompt.length()), ' ');

    if (showHelp || showList) {
        int helpLineStart = max(0, H - 19);
        if (showHelp) {
            ss << "\x1b[" << helpLineStart + 1 << ";1H" << RESET << "Available commands:" << endl;
            ss << "\x1b[" << helpLineStart + 2 << ";1H" << RESET << "  max_rains <int> : Set the maximum number of rains." << endl;
            ss << "\x1b[" << helpLineStart + 3 << ";1H" << RESET << "  min_gap <int>    : Set the minimum gap between rains." << endl;
            ss << "\x1b[" << helpLineStart + 4 << ";1H" << RESET << "  frame_ms <int>   : Set the frame delay in milliseconds." << endl;
            ss << "\x1b[" << helpLineStart + 5 << ";1H" << RESET << "  density <double> : Set the rain density." << endl;
            ss << "\x1b[" << helpLineStart + 6 << ";1H" << RESET << "  base_speed <double>: Set the base rain speed." << endl;
            ss << "\x1b[" << helpLineStart + 7 << ";1H" << RESET << "  speed_var <double>: Set the rain speed variation." << endl;
            ss << "\x1b[" << helpLineStart + 8 << ";1H" << RESET << "  fade <double>    : Set the rain fade rate." << endl;
            ss << "\x1b[" << helpLineStart + 9 << ";1H" << RESET << "  flicker <double> : Set the rain flicker rate." << endl;
            ss << "\x1b[" << helpLineStart + 10 << ";1H" << RESET << "  bias <double>    : Set the rain bias." << endl;
            ss << "\x1b[" << helpLineStart + 11 << ";1H" << RESET << "  max_rain_length <int>: Set the base max rain length." << endl;
            ss << "\x1b[" << helpLineStart + 12 << ";1H" << RESET << "  rain_length_var <int>: Set the rain length variation." << endl;
            ss << "\x1b[" << helpLineStart + 13 << ";1H" << RESET << "  head_char_freq <double>: Set the head char probability." << endl;
            ss << "\x1b[" << helpLineStart + 14 << ";1H" << RESET << "  chars <string>: Set the character set (basic, katakana, both)." << endl;
            ss << "\x1b[" << helpLineStart + 15 << ";1H" << RESET << "  segment_config <char_set> <new_char_set>: Config all segments with given character_set appearance or 'list' to show availables" << endl;
            ss << "\x1b[" << helpLineStart + 16 << ";1H" << RESET << "  rain_color <color_code> <new_color_code>: Config all segments with given color using ANSI code or 'list' to show availables." << endl;
            ss << "\x1b[" << helpLineStart + 17 << ";1H" << RESET << "  back             : Go back to animation." << endl;
            ss << "\x1b[" << helpLineStart + 18 << ";1H" << RESET << "  resize           : Resize the terminal to default values." << endl;
        }
        if (showList) {
            ss << "\x1b[" << helpLineStart + 1 << ";1H" << RESET << listText;
        }
    }


    ss << RESET;
    return ss.str();
}

bool kbhit_nonblock() {
    struct timeval tv;
    fd_set fds;
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv);
    return FD_ISSET(STDIN_FILENO, &fds);
}

int getch_nonblock() {
    int ch = getchar();
    return ch;
}

void get_terminal_size(int& width, int& height) {
#ifdef TIOCGSIZE
    struct ttysize ts;
    ioctl(STDIN_FILENO, TIOCGSIZE, &ts);
    width = ts.ts_cols;
    height = ts.ts_lines;
#elif defined(TIOCGWINSZ)
    struct winsize ws;
    ioctl(STDIN_FILENO, TIOCGWINSZ, &ws);
    width = ws.ws_col;
    height = ws.ws_row;
#else
    width = 80;
    height = 25;
#endif
}

void resizeConsole(deque<rain>& rains) {
    int newW, newH;
    get_terminal_size(newW, newH);

    if (newW != W || newH != H) {
        deque<pair<int, double>> rainData;
        for (const auto& t : rains) {
            rainData.push_back({ t.x, t.y / H });
        }

        rains.clear();
        W = newW;
        H = newH;
        deque<int> rainXPos;

        auto addrain = [&](int x, double relativeY = (double)rng_int(0, H) / H) {
            bool valid = true;
            for (int xp : rainXPos) {
                if (abs(x - xp) < MIN_GAP) {
                    valid = false;
                    break;
                }
            }
            if (valid) {
                rainXPos.push_back(x);
                rains.push_back({ x, relativeY * H, BASE_SPEED + (rng_double() * 2.0 - 1.0) * SPEED_VAR, {}, MAX_rain_LENGTH + (int)((rng_double() * 2.0 - 1.0) * rain_LENGTH_VAR) });
            }
            };

        for (auto& data : rainData) {
            addrain(data.first, data.second);
        }

        for (int i = rainData.size(); i < (int)(W * DENSITY) && rains.size() < MAX_rainS; i++) {
            addrain(rng_int(0, W - 1));
        }
    }
}

void refreshList(const string& lastListCommand, bool& showHelp, bool& showList, string& listText) {
    stringstream ss(lastListCommand);
    string cmd;
    ss >> cmd;
     showList = false;

    if (cmd == "rain_color") {
        string colorCode;
        if (ss >> colorCode && colorCode == "list") {
            listText = "Available colors:\n";
            for (const auto& color : rain_COLORS) {
                for (int i = 0; i < 256; ++i)
                {
                    if (("\x1b[38;5;" + to_string(i) + "m") == string(color)) {
                        listText += to_string(i) + "  " + color + "█" + RESET + "\n";
                        break;
                    }
                }
            }
            showList = true;
           
        }
    }
    else if (cmd == "segment_config")
    {
        string charSet;
        if (ss >> charSet && charSet == "list")
        {
            listText = "Available character sets: basic, katakana, both";
            showList = true;
        }
    }

}

void addrain(int x, deque<int>& rainXPos, deque<rain>& rains, double relativeY = -1.0) {
    if (relativeY < 0) relativeY = (double)rng_int(0, H) / H;
    bool valid = true;
    for (int xp : rainXPos) {
        if (abs(x - xp) < MIN_GAP) {
            valid = false;
            break;
        }
    }
    if (valid) {
        rainXPos.push_back(x);
        rains.push_back({ x, relativeY * H, BASE_SPEED + (rng_double() * 2.0 - 1.0) * SPEED_VAR, {}, MAX_rain_LENGTH + (int)((rng_double() * 2.0 - 1.0) * rain_LENGTH_VAR) });
    }
}

void processValue(const string& cmd, const string& val, bool& refreshrains) {
    try {
        if (val == "list") return;
        if (cmd == "chars") {
            if (val == "basic") chars = chars_basic;
            else if (val == "katakana") chars = chars_katakana;
            else if (val == "both") chars = chars_basic + chars_katakana;
            else throw runtime_error("Invalid chars value. Use 'basic', 'katakana', or 'both'");
        }
        else {
            double numVal = stod(val);
            if (cmd == "max_rains") MAX_rainS = (int)numVal;
            else if (cmd == "min_gap") MIN_GAP = (int)numVal;
            else if (cmd == "frame_ms") FRAME_MS = (int)numVal;
            else if (cmd == "density") DENSITY = numVal;
            else if (cmd == "base_speed") BASE_SPEED = numVal;
            else if (cmd == "speed_var") SPEED_VAR = numVal;
            else if (cmd == "fade") FADE = numVal;
            else if (cmd == "flicker") FLICKER = numVal;
            else if (cmd == "bias") BIAS = numVal;
            else if (cmd == "max_rain_length") MAX_rain_LENGTH = (int)numVal;
            else if (cmd == "rain_length_var") rain_LENGTH_VAR = (int)numVal;
            else if (cmd == "head_char_freq") HEAD_CHAR_FREQ = numVal;
            else throw runtime_error("Unknown command");
        }
        refreshrains = true;
    }
    catch (const exception&) {
        fprintf(stderr, "Invalid value for %s\n", cmd.c_str());
    }
}

void configureSegments(const string& cmd, const string& param1, const string& param2, bool& showHelp, bool& showList, string& listText, string& lastListCommand) {
     showList = false;

    if (param1 == "list") {
        lastListCommand = cmd + " list";
        refreshList(lastListCommand, showHelp, showList, listText);
        showList = true;
        return;
    }

    if (param2.empty()) {
        fprintf(stderr, "Invalid format. Use <%s> <value> or 'list' to see availables.\n", cmd.c_str());
        return;
    }

    if (cmd == "rain_color") {
        string fullNewColorCode = "\x1b[38;5;" + param2 + "m";
        for (auto& segType : SEGMENT_TYPES) {
            if (segType.color == "\x1b[38;5;" + param1 + "m") {
                segType.color = fullNewColorCode;
            }
        }
    }
    else if (cmd == "segment_config") {
        for (auto& segType : SEGMENT_TYPES) {
            if (segType.charSet == param1) {
                segType.charSet = param2;
            }
        }
    }
}

void handleCommand(const string& command, deque<rain>& rains, bool& showHelp, bool& showList, string& listText, string& lastListCommand) {
    stringstream ss(command);
    string cmd, param1, param2;
    ss >> cmd;
    bool refreshrains = false;

    if (cmd == "help") {
        showHelp = true;
        return;
    }
    if (cmd == "back") {
        showHelp = showList = false;
        listText = lastListCommand = "";
        return;
    }
     if (cmd == "resize") {
        W = 80;
        H = 25;
        refreshrains = true;
        }

    ss >> param1;
    if (cmd == "segment_config" || cmd == "rain_color") {
        ss >> param2;
        configureSegments(cmd, param1, param2, showHelp, showList, listText, lastListCommand);
         if (param1 != "list") {
            showHelp = false;
        }
    }
    else if (!cmd.empty()) {
        processValue(cmd, param1, refreshrains);
        showHelp = false;
    }

    if (refreshrains) {
        rains.clear();
        deque<int> rainXPos;
        for (int i = 0; i < (int)(W * DENSITY) && rains.size() < MAX_rainS; i++) {
            addrain(rng_int(0, W - 1), rainXPos, rains);
        }
    }
    if (showList) showHelp = false;
}

termios original_termios;

void restore_terminal() {
    tcsetattr(STDIN_FILENO, TCSANOW, &original_termios);
}

void signal_handler(int signal) {
    restore_terminal();
    exit(0);
}

void enable_raw_mode() {
  termios raw;
  tcgetattr(STDIN_FILENO, &original_termios);
  raw = original_termios;
  raw.c_lflag &= ~(ECHO | ICANON);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 0;
  tcsetattr(STDIN_FILENO, TCSANOW, &raw);
}


int main() {

    tcgetattr(STDIN_FILENO, &original_termios);

    signal(SIGINT, signal_handler);

    atexit(restore_terminal);

    enable_raw_mode();

    try {
        deque<rain> rains;
        deque<int> rainXPos;

        for (int i = 0; i < (int)(W * DENSITY) && rains.size() < MAX_rainS; i++) {
            addrain(rng_int(0, W - 1), rainXPos, rains);
        }

        string commandLine;
        bool showHelp = false;
        bool showList = false;
        string listText = "";
        string lastListCommand = "";
        bool toggleHelpNextFrame = false;

        while (true) {
            resizeConsole(rains);

            vector<future<void>> futures;
            for (auto& t : rains) {
                futures.push_back(async(launch::async, [&]() { update(t); }));
            }
            for (auto& future : futures) {
                future.get();
            }

            future<string> render_future = async(launch::async, render, ref(rains), ref(commandLine), showHelp, showList, ref(listText));

            if (kbhit_nonblock()) {
                int ch = getch_nonblock();

                if (ch == 10) {
                    handleCommand(commandLine, rains, showHelp, showList, listText, lastListCommand);
                     if (showList) showHelp = false;
                    else  showHelp = false;
                    commandLine = "";
                } else if (ch == 127 || ch == 8) {
                    if (!commandLine.empty()) {
                        commandLine.pop_back();
                    }
                }  else if (ch == 27) {
                    toggleHelpNextFrame = true;
                }
                else if (ch >= 32 && ch <= 126) {
                    commandLine += static_cast<char>(ch);
                }
            }
            if (toggleHelpNextFrame) {
                showHelp = !showHelp;
                showList = false;
                toggleHelpNextFrame = false;
            }

            string rendered_frame = render_future.get();
            printf("\x1b[H");
            printf("%s", rendered_frame.c_str());

            usleep(FRAME_MS * 1000);
        }
    }
    catch (const exception& e) {
        fprintf(stderr, "Error: %s\n", e.what());
        restore_terminal();
        return 1;
    }

    return 0;
}