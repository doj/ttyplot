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

#define verstring "https://github.com/doj/ttyplot"

#define DOUBLE_MIN (-FLT_MAX)
#define DOUBLE_MAX FLT_MAX

#ifdef NOACS
#define T_HLINE '-'
#define T_VLINE '|'
#define T_RARR '>'
#define T_UARR '^'
#define T_LLCR 'L'
#else
#define T_HLINE ACS_HLINE
#define T_VLINE ACS_VLINE
#define T_RARR ACS_RARROW
#define T_UARR ACS_UARROW
#define T_LLCR ACS_LLCORNER
#endif

/* global because we need it accessible in the signal handler */
SCREEN *sp;

void usage() {
  printf("Usage: ttyplot [-2] [-k] [-r] [-c char] [-e char] [-E char] [-s scale] [-S scale] [-m max] [-M min] [-t title] [-u unit]\n\n"
         "  -2 read two values and draw two plots\n"
         "  -k key/value mode\n"
         "  -r rate of a counter (divide value by measured sample interval)\n"
         "  -c character(s) for the graph, not used with key/value mode\n"
         "  -e character to use for error line when value exceeds hardmax, default: 'e'\n"
         "  -E character to use for error symbol displayed when value is less than hardmin, default: 'v'\n"
         "  -s initial positive scale of the plot (can go above if data input has larger value)\n"
         "  -S initial negative scale of the plot\n"
         "  -m maximum value, if exceeded draws error line (see -e), upper-limit of plot scale is fixed\n"
         "  -M minimum value, if entered less than this, draws error symbol (see -E), lower-limit of the plot scale is fixed\n"
         "  -t title of the plot\n"
         "  -u unit displayed beside vertical bar\n");
  exit(EXIT_FAILURE);
}

void draw_axes(const int screenheight,
               const int plotheight,
               const int plotwidth)
{
  // x axis
  mvhline(screenheight-3, 1, T_HLINE, plotwidth-1);
  mvaddch(screenheight-3, plotwidth, T_RARR);
  // y axis
  mvvline(1, 0, T_VLINE, plotheight-1);
  mvaddch(0, 0, T_UARR);
  // corner
  mvaddch(screenheight-3, 0, T_LLCR);
}

void draw_labels(const int plotheight,
                 const double max,
                 const double min,
                 const char *unit)
{
  attron(A_BOLD);
  mvprintw(0,              1, "%.1f %s", max,             unit);
  mvprintw(plotheight/4,   1, "%.1f %s", min/4 + max*3/4, unit);
  mvprintw(plotheight/2,   1, "%.1f %s", min/2 + max/2,   unit);
  mvprintw(plotheight*3/4, 1, "%.1f %s", min*3/4 + max/4, unit);
  mvprintw(plotheight,     1, "%.1f %s", min,             unit);
  attroff(A_BOLD);
}

void draw_line(const int x,
               int y1,
               int y2,
               const char pc)
{
    if (y1 == INT_MIN ||
        y1 == y2)
    {
      mvaddch(y2, x, pc);
      return;
    }
    if (y1 > y2)
    {
      std::swap(y1, y2);
    }
    assert(y1 < y2);
    mvvline(y1, x, pc, y2-y1);
}

volatile bool sigwinch_received = false;
void resize(int sig) {
    (void) sig;
    sigwinch_received = true;
}

void finish(int sig) {
    (void) sig;
    curs_set(FALSE);
    echo();
    refresh();
    endwin();
    delscreen(sp);
    exit(EXIT_SUCCESS);
}

struct values_t
{
  std::deque<double> vec;
  double cval = DOUBLE_MAX;
  double pval = DOUBLE_MAX;
  double max;
  double min;
  double avg;
  char plotchar;

  void init(const std::string &s)
  {
    assert(! s.empty());
    plotchar = s[0];
  }

  void push_back(const double v, const size_t plotwidth)
  {
    vec.push_back(v);
    if (vec.size() > plotwidth)
      vec.pop_front();
  }

  void update()
  {
    double tot = 0;
    min = DOUBLE_MAX;
    max = DOUBLE_MIN;
    size_t i = 0;
    for(const auto val : vec)
    {
      if(val > max)
        max = val;
      if(val < min)
        min = val;
      tot += val;
      ++i;
    }
    avg = tot / i;
  }

  /**
   * before calling plot(), update() should be called.
   * @param idx index of plot that is drawn.
   * @param n highest index of valid values in @p v
   */
  void plot(const unsigned idx,
            const int plotheight,
            const double global_max,
            const double global_min,
            const char max_errchar,
            const char min_errchar,
            const double hardmax,
            const char *unit) const
  {
    // x screen coordinate
    int x = 0;
    // y screen coordinate of previous row
    int lasty = INT_MIN;
    const double mymax = global_max - global_min;

    for(const auto val : vec)
    {
      const int y = (val>=hardmax) ? plotheight : (val<=global_min) ? 0 : (int)(((val-global_min)/mymax)*(double)plotheight);
      draw_line(x++, lasty, y, (val>hardmax) ? max_errchar : (val<global_min) ? min_errchar : plotchar);
      lasty = y;
    }

    mvprintw(plotheight + idx + 1, 5, "%c last=%.1f min=%.1f max=%.1f avg=%.1f %s", plotchar, vec.back(), min, max, avg, unit);
  }
};

std::map<std::string, values_t> values;
void push_back(const std::string &s, const double v, const size_t plotwidth)
{
  if (s.empty())
    return;
  auto it = values.find(s);
  if (it != values.end())
  {
    it->second.push_back(v, plotwidth);
    return;
  }
  auto &val = values[s];
  val.init(s);
  val.push_back(v, plotwidth);
}

int main(int argc, char *argv[]) {
  const std::string one_str = "1";
  const std::string two_str = "2";
    int plotwidth=0, plotheight=0;
    time_t t1;
    int c;
    char max_errchar='e', min_errchar='v';
    double softmax=DOUBLE_MIN;
    double softmin=DOUBLE_MAX;
    double hardmax=DOUBLE_MAX;
    double hardmin = DOUBLE_MIN;
    const char *title = NULL;
    const char *unit = "";
    int rate=0;

    enum class OperatingMode {
      ONE, TWO, KV
    } op_mode = OperatingMode::ONE;

    opterr=0;
    while((c=getopt(argc, argv, "2krc:e:E:s:S:m:M:t:u:")) != -1)
        switch(c) {
            case 'r':
                rate=1;
                break;
            case '2':
              op_mode = OperatingMode::TWO;
              break;
          case 'k':
              op_mode = OperatingMode::KV;
              break;
            case 'c':
#if 0
              //todo
              if (op_mode = OperatingMode::ONE;
              values[one_str].plotchar = optarg[0];
              if (two)
              {
                values[two_str].plotchar = optarg[1];
              }
#endif
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

    if(softmax <= hardmin)
        softmax = hardmin + 1;
    if(hardmax <= hardmin)
        hardmax = DOUBLE_MAX;

    #ifdef __OpenBSD__
    if (pledge("stdio tty", NULL) == -1)
        err(1, "pledge");
    #endif

    sp = newterm(NULL, stdout, stdin);

    time(&t1);
    noecho();
    curs_set(FALSE);
    signal(SIGWINCH, resize);
    signal(SIGINT, finish);

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

    double global_max = DOUBLE_MIN;
    double global_min = DOUBLE_MAX;
    while(1) {
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
          push_back(one_str, v, plotwidth);
        }
      }
      else if (op_mode == OperatingMode::TWO)
      {
        double v1, v2;
        r = scanf("%lf %lf", &v1, &v2);
        if (r == 2)
        {
          push_back(one_str, v1, plotwidth);
          push_back(two_str, v2, plotwidth);
        }
      }
      else if (op_mode == OperatingMode::KV)
      {
        std::string line;
        std::getline(std::cin, line);
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
          push_back(key, v, plotwidth);
          ++r;
        }
      }
      else
      {
        assert(false);
      }

      if(r == 0)
      {
        while(getchar()!='\n') {}
        continue;
      }
      else if(r < 0)
      {
        break;
      }
#if 0
        if(rate) {
            t2=t1;
            time(&t1);
            td=t1-t2;
            if(td==0)
                td=1;

            if(cval1==DOUBLE_MAX)
                pval1=values1[n];
            else
                pval1=cval1;
            cval1=values1[n];

            values1[n]=(cval1-pval1)/td;

            if(values1[n] < 0) // counter rewind
                values1[n]=0;

            if(two) {
                if(cval2==DOUBLE_MAX)
                    pval2=values2[n];
                else
                    pval2=cval2;
                cval2=values2[n];

                values2[n]=(cval2-pval2)/td;

                if(values2[n] < 0) // counter rewind
                    values2[n]=0;
            }
        } else
#endif
        {
            time(&t1);
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
        plotheight=screenheight-3;
        plotwidth=screenwidth;

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

        if(global_max < softmax)
          global_max = softmax;
        if(hardmax != DOUBLE_MAX)
            global_max = hardmax;
        if(softmin < global_min)
          global_min = softmin;
        if(hardmin != DOUBLE_MIN)
          global_min = hardmin;

        // print program version string
        mvprintw(screenheight-1, screenwidth-sizeof(verstring)+1, verstring);
        // print current time
        {
          char ls[32];
          auto lt = localtime(&t1);
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
          mvprintw(screenheight-2, screenwidth-len, "%s", ls);
        }

#if 0
        if(rate)
            printw(" interval=%llds", (long long int)td);
#endif

        draw_axes(screenheight, plotheight, plotwidth);
        unsigned idx = 0;
        for(const auto &p : values)
        {
          p.second.plot(idx++, plotheight, global_max, global_min, max_errchar, min_errchar, hardmax, unit);
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
