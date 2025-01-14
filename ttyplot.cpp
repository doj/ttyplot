/** @file
 * ttyplot: a realtime plotting utility for terminal with data input from stdin
 * Copyright (c) 2018 by Antoni Sawicki
 * Copyright (c) 2019 by Google LLC
 * Copyright (c) 2022 by Dirk Jagdmann <doj@cubic.org>
 * Apache License 2.0
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <float.h>
#include <time.h>
#include <curses.h>
#include <signal.h>
#include <sys/time.h>
#include <execinfo.h>

#ifdef __OpenBSD__
#include <err.h>
#endif

#include <utility>
#include <cassert>
#include <climits>
#include <map>
#include <string>
#include <deque>
#include <iostream>
#include <sstream>
#include <vector>
#include <algorithm>

#define verstring "github.com/doj/ttyplot"

#define DOUBLE_MIN (-FLT_MAX)
#define DOUBLE_MAX FLT_MAX
#define DOUBLE_UNINIT DOUBLE_MIN

#define INT_UNINIT INT_MIN

#define CHAR_REVERSE ' '

#define SCREENWIDTH_FOR_2COLUMN 140

#ifdef NOACS
#define T_HLINE '-'
#define T_VLINE '|'
#define T_RARR '>'
#define T_UARR '^'
#define T_LLCR 'L'
#define T_BLOCK '#'
#else
#define T_HLINE ACS_HLINE
#define T_VLINE ACS_VLINE
#define T_RARR ACS_RARROW
#define T_UARR ACS_UARROW
#define T_LLCR ACS_LLCORNER
#define T_BLOCK ACS_BLOCK
#endif

const char *debug_fn = "/tmp/ttyplot.txt";

void
debug(const char *fmt, ...)
{
  auto f = fopen(debug_fn, "a");
  if (! f)
    return;
  va_list ap;
  va_start(ap, fmt);
  vfprintf(f, fmt, ap);
  va_end(ap);
  fclose(f);
}

/* global because we need it accessible in the signal handler */
SCREEN *sp;

void
usage()
{
  printf("Usage: ttyplot [-2] [-k] [-r] [-b] [-c char] [-e char] [-E char] [-s scale] [-S scale] [-m max] [-M min] [-t title] [-u unit] [-C 'col1 col2 ...']\n\n"
         "  -2 read two values and draw two plots\n"
         "  -k key/value mode\n"
         "  -r rate mode (divide value by measured sample interval)\n"
         "  -b draw bar charts, should be set before -2\n"
         "  -c character(s) for the graph, not used with key/value mode, should be set after -2\n"
         "  -e character to use for error line when value exceeds hardmax, default: 'e'\n"
         "  -E character to use for error symbol displayed when value is less than hardmin, default: 'v'\n"
         "  -s initial maximum value of the plot\n"
         "  -S initial minimum of the plot\n"
         "  -m maximum value, if exceeded draws error line (see -e), upper-limit of plot scale is fixed\n"
         "  -M minimum value, if entered less than this, draws error symbol (see -E), lower-limit of the plot scale is fixed\n"
         "  -t title of the plot\n"
         "  -u unit displayed on vertical bar\n"
         "  -C set list of colors: black,blk,bk  red,rd  green,grn,gr  yellow,yel,yl  blue,blu,bl  magenta,mag,mg  cyan,cya,cy,cn  white,wht,wh\n"
         "\nfor more information visit https://%s\n", verstring
         );
  exit(EXIT_FAILURE);
}

void
draw_axes(const int plotheight, const int plotwidth)
{
  // x axis
  mvhline(plotheight, 1, T_HLINE, plotwidth-1);
  mvaddch(plotheight, plotwidth-1, T_RARR);
  // y axis
  mvvline(1, 0, T_VLINE, plotheight-1);
  mvaddch(0, 0, T_UARR);
  // corner
  mvaddch(plotheight, 0, T_LLCR);
}

std::string
printValue(const double d, const char *unit)
{
  std::string s;
  if (d < +0.01 &&
      d > -0.01)
  {
    s = "0";
  }
  else
  {
    char buf[16];
    int len = snprintf(buf, sizeof(buf), "%.2f", d);
    s.assign(buf, len);
    auto pos = s.find('.');
    if (pos != std::string::npos)
    {
      while(s.back() == '0')
      {
        s.pop_back();
      }
      if (s.back() == '.')
      {
        s.pop_back();
      }
    }
  }

  if (unit)
  {
    s += ' ';
    s += unit;
  }
  return s;
}

void
draw_labels(const int plotheight,
            const double max,
            const double min,
            const char *unit)
{
  attron(A_BOLD);
  mvprintw(0,              1, "%s", printValue(max,             unit).c_str());
  mvprintw(plotheight/4,   1, "%s", printValue(min/4 + max*3/4, unit).c_str());
  mvprintw(plotheight/2,   1, "%s", printValue(min/2 + max/2,   unit).c_str());
  mvprintw(plotheight*3/4, 1, "%s", printValue(min*3/4 + max/4, unit).c_str());
  mvprintw(plotheight-1,   1, "%s", printValue(min,             unit).c_str());
  attroff(A_BOLD);
}

void
draw_line(const int x,
          int y1,
          int y2,
          int pc)
{
  if (pc == '#')
  {
    pc = T_BLOCK;
  }
  if (y1 == y2)
  {
    if (pc == CHAR_REVERSE)
    {
      mvchgat(y1, x, 1, A_REVERSE, COLOR_PAIR(0), NULL);
    }
    else
    {
      mvaddch(y1, x, pc);
    }
    return;
  }
  if (y1 > y2)
  {
    std::swap(y1, y2);
  }
  assert(y1 < y2);
  if (pc == CHAR_REVERSE)
  {
    for(; y1 < y2; ++y1)
    {
      mvchgat(y1, x, 1, A_REVERSE, COLOR_PAIR(0), NULL);
    }
  }
  else
  {
    mvvline(y1, x, pc, y2-y1);
  }
}

volatile bool sigwinch_received = false;
void
resize(int sig)
{
  (void) sig;
  sigwinch_received = true;
}

void
finish(int sig)
{
  (void) sig;
  curs_set(FALSE);
  echo();
  refresh();
  endwin();
  delscreen(sp);
  if (sig == SIGSEGV)
  {
    void* array[50];
    const int frames = backtrace(array, 50);
    fprintf(stderr, "\nprocess received SIGSEGV\n");
    backtrace_symbols_fd(array, frames, 1);
    exit(EXIT_FAILURE);
  }
  exit(EXIT_SUCCESS);
}

struct values_t
{
  // values of the graph
  std::deque<double> vec;
  // previous real value, used in rate mode
  double pval;
  // maximum value in vec
  double max;
  // minimum value in vec
  double min;
  // average value in vec
  double avg;
  // median value in vec
  double med;
  // graph name
  std::string name;
  // if true plot bars
  bool bars;
  // if true a value was pushed back in this update cycle
  bool did_push_back;
  // maximum size of vec of all values_t objects.
  static size_t max_size;

  void init(std::string s)
  {
    assert(! s.empty());
    name = std::move(s);
  }

  void push_back(const double cval, const size_t plotwidth, const bool b)
  {
    bars = b;
    did_push_back = true;
    // if this vector contains less elements than the largest other vector,
    // resize this vector to the max size.
    if (max_size > 0 &&
        vec.size() < max_size - 1u)
    {
      vec.resize(max_size - 1u, DOUBLE_UNINIT);
    }
    // add the current value
    vec.push_back(cval);
    // remove first value if we store more than plotwidth
    if (vec.size() > plotwidth)
    {
      vec.pop_front();
    }
    // update max_size
    if (vec.size() > max_size)
    {
      max_size = vec.size();
    }
  }

  /// change the last value in the vector to a rate value
  void rate(const double td)
  {
    const auto s = vec.size();
    if (s == 0)
      return;

    if (s == 1)
    {
      pval = vec[0];
      vec[0] = 0;
      return;
    }

    // the current value which was just added to the vector
    const double cval = vec[s - 1];

    // detect 32 bit overflow
    if (pval >= 0xffffff00 &&
        cval >= 0.0 &&
        cval < 0xff)
    {
      vec[s - 1] = cval + (pval - 0xffffff00);
    }
    // detect 31 bit overflow
    else if (pval >= 0x7fffff00 &&
             pval <= 0x7fffffff &&
             cval >= 0.0 &&
             cval < 0xff)
    {
      vec[s - 1] = cval + (pval - 0x7fffff00);
    }
    else
    {
      vec[s - 1] -= pval;
    }
    vec[s - 1] /= td;
    pval = cval;
  }

  /// calculate min, avg, max.
  void update()
  {
    if (vec.empty())
    {
      min = max = avg = med = 0.0;
      return;
    }
    double tot = 0;
    min = DOUBLE_MAX;
    max = DOUBLE_MIN;
    size_t i = 0;
    std::vector<double> med_vec;
    med_vec.reserve(vec.size());
    for(const auto val : vec)
    {
      if (val == DOUBLE_UNINIT)
        continue;
      if (val > max)
        max = val;
      if (val < min)
        min = val;
      tot += val;
      ++i;
      med_vec.push_back(val);
    }

    if (med_vec.empty())
    {
      min = max = avg = med = 0.0;
      return;
    }

    avg = tot / i;

    std::sort(med_vec.begin(), med_vec.end());
    med = med_vec[med_vec.size() / 2];
  }

  /**
   * before calling plot(), update() should be called.
   * @param idx index of plot that is drawn.
   * @param n highest index of valid values in @p v
   */
  void plot(const unsigned idx,
            const int screenwidth,
            const int plotheight,
            const double global_max,
            const double global_min,
            const char max_errchar,
            const char min_errchar,
            const double hardmax) const
  {
    if (vec.empty())
    {
      return;
    }

    // y screen coordinate of previous row
    int lasty = INT_UNINIT;
    const double mymax = global_max - global_min;

    for(unsigned x = 0; x < vec.size(); ++x)
    {
      const auto val = vec[x];
      // skip points which have not been initialized
      if (val == DOUBLE_UNINIT)
      {
        lasty = INT_UNINIT;
        continue;
      }
      char pc;  // plot character
      int y;    // y coordinate of the value
      // check for min and max
      if (val >= hardmax)
      {
        y = 0;
        pc = max_errchar;
      }
      else if (val <= global_min)
      {
        y = plotheight - 1;
        pc = min_errchar;
      }
      // regular point
      else
      {
        y = plotheight - static_cast<int>((val-global_min) / mymax * plotheight) - 1;
        if (y < 0)
          y = 0;
        pc = name[0];
      }
      // adjust lasty to draw a bar or a point
      if (bars)
      {
        lasty = plotheight;
      }
      else if (lasty == INT_UNINIT)
      {
        lasty = y;
      }
      // draw
      draw_line(x+1, lasty, y, pc);
      lasty = y;
    }

    // calculate screen position of details
    int x, y;
    if (screenwidth < SCREENWIDTH_FOR_2COLUMN)
    {
      x = 0;
      y = plotheight + idx + 1;
    }
    else
    {
      x = (idx & 1) * SCREENWIDTH_FOR_2COLUMN / 2;
      y = plotheight + idx/2 + 1;
    }
    // print the detail name
    if (name[0] == CHAR_REVERSE &&
        name.size() == 1)
    {
      attron(A_REVERSE);
      mvaddch(y, x, CHAR_REVERSE);
      attroff(A_REVERSE);
    }
    else
    {
      mvprintw(y, x, "%s", name.c_str());
    }
    // print details
    printw(" last=%.1f min=%.1f max=%.1f avg=%.1f med=%.1f ", last(), min, max, avg, med);
  }

  /// @return last valid value.
  /// @return 0 if no valid value is found.
  double last() const
  {
    for(auto it = vec.rbegin(); it != vec.rend(); ++it)
    {
      if (*it != DOUBLE_UNINIT)
      {
        return *it;
      }
    }
    return 0;
  }
};

size_t values_t::max_size = 0;

std::map<std::string, values_t> values;
void
push_back(const std::string &s, const double v, const size_t plotwidth, const bool bars)
{
  if (s.empty())
    return;
  auto it = values.find(s);
  if (it != values.end())
  {
    it->second.push_back(v, plotwidth, bars);
    return;
  }
  auto &val = values[s];
  val.init(s);
  val.push_back(v, plotwidth, bars);
}

int
parseColors(const std::string &color_str)
{
  if (color_str.empty())
    return -1;
  bool ret = true;
  int parsed_colors = 0;
  std::istringstream is(color_str);
  while(is)
  {
    std::string col_str;
    if (is >> col_str)
    {
      int col = -1;
      if (col_str == "black" ||
          col_str == "blk" ||
          col_str == "bk")
      {
        col = COLOR_BLACK;
      }
      else if (col_str == "red" ||
               col_str == "rd")
      {
        col = COLOR_RED;
      }
      else if (col_str == "green" ||
               col_str == "grn" ||
               col_str == "gr")
      {
        col = COLOR_GREEN;
      }
      else if (col_str == "yellow" ||
               col_str == "yel" ||
               col_str == "yl")
      {
        col = COLOR_YELLOW;
      }
      else if (col_str == "blue" ||
               col_str == "blu" ||
               col_str == "bl")
      {
        col = COLOR_BLUE;
      }
      else if (col_str == "magenta" ||
               col_str == "mag" ||
               col_str == "mg")
      {
        col = COLOR_MAGENTA;
      }
      else if (col_str == "cyan" ||
               col_str == "cyn" ||
               col_str == "cy" ||
               col_str == "cn")
      {
        col = COLOR_CYAN;
      }
      else if (col_str == "white" ||
               col_str == "wht" ||
               col_str == "wh")
      {
        col = COLOR_WHITE;
      }
      else
      {
        printf("unknown color: %s\n", col_str.c_str());
        ret = false;
        continue;
      }
      assert(col >= 0);
      // \todo get default background color
      int res = init_pair(++parsed_colors, col, COLOR_BLACK);
      assert(res == OK);
    }
  }
  if (! ret)
  {
    exit(EXIT_FAILURE);
  }
  return parsed_colors;
}

/// @return number of milliseconds since unix epoch.
size_t
getms()
{
  size_t ms = 0;
  struct timeval tv;
  if (gettimeofday(&tv, NULL) == 0)
  {
    ms = tv.tv_sec;
    ms *= 1000u;
    ms += tv.tv_usec / 1000u;
  }
  return ms;
}

int
main(int argc, char *argv[])
{
  const std::string one_str = "1";
  const std::string two_str = "2";
  int plotwidth=0, plotheight=0;
  int c;
  int parsed_colors = -1;
  char max_errchar='e', min_errchar='v';
  double softmax=DOUBLE_MIN;
  double softmin=DOUBLE_MAX;
  double hardmax=DOUBLE_MAX;
  double hardmin = DOUBLE_MIN;
  const char *title = NULL;
  const char *unit = NULL;
  std::string color_str;
  bool rate = false;
  bool bars = false;

  enum class OperatingMode {
    ONE, TWO, KV
  } op_mode = OperatingMode::ONE;

  values[one_str].name = '#';

  while((c=getopt(argc, argv, "2bkrc:C:e:E:s:S:m:M:t:u:")) != -1)
    switch(c) {
      case 'b':
        bars = true;
        values[one_str].name = '|';
        break;
      case 'r':
        rate = true;
        break;
      case '2':
        op_mode = OperatingMode::TWO;
	values[two_str].name = CHAR_REVERSE;
        break;
      case 'k':
        op_mode = OperatingMode::KV;
        values.clear();
        break;
      case 'C':
        color_str = optarg;
        break;
      case 'c':
        if (op_mode == OperatingMode::ONE)
        {
          values[one_str].name = optarg[0];
        }
        else if (op_mode == OperatingMode::TWO)
        {
          values[one_str].name = optarg[0];
          values[two_str].name = optarg[1];
        }
        else
        {
          printf("command line argument -c ignored in key/value mode\n");
        }
        break;
      case 'e':
        max_errchar=optarg[0];
        break;
      case 'E':
        min_errchar=optarg[0];
        break;
      case 's':
        softmax=atof(optarg);
        break;
      case 'S':
        softmin=atof(optarg);
        break;
      case 'm':
        hardmax=atof(optarg);
        break;
      case 'M':
        hardmin=atof(optarg);
        break;
      case 't':
        title = optarg;
        break;
      case 'u':
        unit = optarg;
        break;
      case '?':
        usage();
        break;
    }

  // unlink(debug_fn);
  if (softmax <= hardmin)
    softmax = hardmin + 1;
  if (hardmax <= hardmin)
    hardmax = DOUBLE_MAX;

#ifdef __OpenBSD__
  if (pledge("stdio tty", NULL) == -1)
    err(1, "pledge");
#endif

  sp = newterm(NULL, stdout, stdin);
  if (! color_str.empty())
  {
    start_color();
    parsed_colors = parseColors(color_str);
  }

  noecho();
  curs_set(FALSE);
  signal(SIGWINCH, resize);
  signal(SIGINT,  finish);
  signal(SIGTERM, finish);
  signal(SIGSEGV, finish);

  erase();
  int screenwidth=0, screenheight=0;
#ifdef NOGETMAXYX
  screenheight=LINES;
  screenwidth=COLS;
#else
  getmaxyx(stdscr, screenheight, screenwidth);
#endif
  mvprintw(screenheight/2, (screenwidth/2)-14, "waiting for data from stdin");
  refresh();

  auto t1 = getms();
  double global_max = DOUBLE_MIN;
  double global_min = DOUBLE_MAX;
  while(1)
  {
    double td = 1;
    if (sigwinch_received)
    {
      sigwinch_received = false;
      endwin();
    }
    int r = 0;
    if (op_mode == OperatingMode::ONE)
    {
      double v;
      r = scanf("%lf", &v);
      if (r == 1)
      {
        push_back(one_str, v, plotwidth, bars);
      }
    }
    else if (op_mode == OperatingMode::TWO)
    {
      double v1, v2;
      r = scanf("%lf %lf", &v1, &v2);
      if (r == 2)
      {
        push_back(one_str, v1, plotwidth, bars);
        push_back(two_str, v2, plotwidth, bars);
      }
    }
    else if (op_mode == OperatingMode::KV)
    {
      // clear the push_back flag for all values
      for(auto &p : values)
      {
        p.second.did_push_back = false;
      }
      // read a line from STDIN
      std::string line;
      std::getline(std::cin, line);
      // parse line for key/value pairs
      std::istringstream is(line);
      while(is)
      {
        std::string key;
        is >> key;
        if (! is)
          break;
        double v;
        is >> v;
        if (! is)
          break;
        push_back(key, v, plotwidth, bars);
        ++r;
      }
      // push the uninitialized value for all values which did not parse a key/value pair
      for(auto &p : values)
      {
        if (p.second.did_push_back == false)
        {
          p.second.push_back(DOUBLE_UNINIT, plotwidth, bars);
          assert(p.second.did_push_back);
        }
      }
    }
    else
    {
      assert(false);
    }

    if (r == 0)
    {
      while(getchar()!='\n') {}
      continue;
    }
    else if (r < 0)
    {
      break;
    }

    if (rate)
    {
      const auto prev_ts = t1;
      t1 = getms();
      assert(prev_ts <= t1);
      const auto tdiff = t1 - prev_ts;
      if (tdiff == 0)
      {
        td = 1;
      }
      else
      {
        td = tdiff / 1000.0;
      }
      for(auto &p : values)
      {
        p.second.rate(td);
      }
    }

    erase();
#ifdef _AIX
    refresh();
#endif
#ifdef NOGETMAXYX
    screenheight=LINES;
    screenwidth=COLS;
#else
    getmaxyx(stdscr, screenheight, screenwidth);
#endif
    if (screenheight < 8)
    {
      mvprintw(0,0,"screen height too small");
      refresh();
      continue;
    }
    if (screenwidth < 40)
    {
      mvprintw(0,0,"screen width too small");
      refresh();
      continue;
    }
    if (screenwidth < SCREENWIDTH_FOR_2COLUMN)
    {
      plotheight = screenheight - values.size() - 1;
    }
    else
    {
      plotheight = screenheight - values.size() / 2 - 2;
    }
    if (plotheight < screenheight / 2)
    {
      plotheight = screenheight / 2;
    }
    plotwidth = screenwidth - 1;

    for(auto &p : values)
    {
      auto &vals = p.second;
      vals.update();
      if (vals.max > global_max)
      {
        global_max = vals.max;
      }
      if (vals.min < global_min)
      {
        global_min = vals.min;
      }
    }

    if (global_max < softmax)
      global_max = softmax;
    if (hardmax != DOUBLE_MAX)
      global_max = hardmax;
    if (softmin < global_min)
      global_min = softmin;
    if (hardmin != DOUBLE_MIN)
      global_min = hardmin;

    // print current time
    {
      time_t t = time(NULL);
      char ls[32];
      auto lt = localtime(&t);
#ifdef __sun
      asctime_r(lt, ls, sizeof(ls));
#else
      asctime_r(lt, ls);
#endif
      auto len = strlen(ls);
      assert(len > 10);
      // strip trailing NL character
      if (ls[len - 1] == '\n')
      {
        ls[--len] = 0;
      }
      mvprintw(screenheight-1, screenwidth-len, "%s", ls);
    }
    // print program version string
    if (values.size() >= 2)
    {
      mvprintw(screenheight-2, screenwidth-sizeof(verstring)+1, verstring);
    }

    if (rate)
    {
      std::string s = "interval=";
      s += std::to_string(td);
      while(s.back() == '0')
        s.pop_back();
      s += 's';
      mvprintw(screenheight-1, screenwidth/2 - s.size()/2,"%s", s.c_str());
    }

    draw_axes(plotheight, plotwidth);
    int idx = 0;
    char last_plotchar = 0;
    for(const auto &p : values)
    {
      int attr = 0;
      // did we parse colors?
      if (parsed_colors > 0)
      {
        // use the color
        attr = COLOR_PAIR((idx % parsed_colors) + 1);
        // if we've used all colors, set some attributes
        if (idx >= parsed_colors &&
            idx < parsed_colors*2)
        {
          attr |= A_BOLD;
        }
        else if (idx >= parsed_colors*2 &&
                 idx < parsed_colors*3)
        {
          attr |= A_STANDOUT;
        }
        else if (idx >= parsed_colors*3 &&
                 idx < parsed_colors*4)
        {
          attr |= A_DIM;
        }
        else
        {
          attr |= A_REVERSE;
        }
      }
      else
      {
        if (op_mode == OperatingMode::KV)
        {
          // check if the previous data point used the same plotchar
          const char plotchar = p.first[0];
          if (plotchar == last_plotchar)
          {
            // set a font attribute to better distinguish the same plotchars
            int arr[4] = {A_BOLD, A_STANDOUT, A_DIM, A_REVERSE};
            attr |= arr[idx & 3];
          }
          last_plotchar = plotchar;
        }
      }

      attron(attr);
      p.second.plot(idx, screenwidth, plotheight, global_max, global_min, max_errchar, min_errchar, hardmax);
      attroff(attr);

      ++idx;
    }

    draw_labels(plotheight, global_max, global_min, unit);
    if (title)
    {
      attron(A_BOLD);
      mvprintw(0, (screenwidth/2)-(strlen(title)/2)-1, " %s ", title);
      attroff(A_BOLD);
    }

    move(0,0);
    refresh();
  }  // while 1

  endwin();
  delscreen(sp);
  return EXIT_SUCCESS;
}
