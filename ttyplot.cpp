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
    printf("Usage: ttyplot [-2] [-r] [-c char] [-e char] [-E char] [-s scale] [-S scale] [-m max] [-M min] [-t title] [-u unit]\n\n"
            "  -2 read two values and draw two plots\n"
            "  -r rate of a counter (divide value by measured sample interval)\n"
            "  -c character to use for plot line, eg @ # %% . etc\n"
            "  -e character to use for error line when value exceeds hardmax (default: e)\n"
            "  -E character to use for error symbol displayed when value is less than hardmin (default: v)\n"
            "  -s initial positive scale of the plot (can go above if data input has larger value)\n"
            "  -S initial negative scale of the plot\n"
            "  -m maximum value, if exceeded draws error line (see -e), upper-limit of plot scale is fixed\n"
            "  -M minimum value, if entered less than this, draws error symbol (see -E), lower-limit of the plot scale is fixed\n"
            "  -t title of the plot\n"
            "  -u unit displayed beside vertical bar\n");
    exit(EXIT_FAILURE);
}

void getminmax(const int pw,
               const double *values,
               double *min,
               double *max,
               double *avg,
               const int v)
{
    double tot = 0;
    *min=DOUBLE_MAX;
    *max=DOUBLE_MIN;
    int i;
    for(i = 0; i < pw && i < v; ++i)
    {
       if(values[i]>*max)
            *max=values[i];

        if(values[i]<*min)
            *min=values[i];

        tot += values[i];
    }

    *avg=tot/i;
}

void draw_axes(const int screenheight,
               const int plotheight,
               const int plotwidth,
               const double max,
               const double min,
               const char *unit)
{
  // x axis
  mvhline(screenheight-3, 1, T_HLINE, plotwidth-1);
  mvaddch(screenheight-3, plotwidth, T_RARR);
  // y axis
  mvvline(1, 0, T_VLINE, plotheight-1);
  mvaddch(0, 0, T_UARR);
  // corner
  mvaddch(screenheight-3, 0, T_LLCR);
  // values
  mvprintw(0,              1, "%.1f %s", max,             unit);
  mvprintw(plotheight/4,   1, "%.1f %s", min/4 + max*3/4, unit);
  mvprintw(plotheight/2,   1, "%.1f %s", min/2 + max/2,   unit);
  mvprintw(plotheight*3/4, 1, "%.1f %s", min*3/4 + max/4, unit);
  mvprintw(plotheight,     1, "%.1f %s", min,             unit);
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
    assert(y1 <= y2);
    mvvline(y1, x, pc, y2-y1);
}

/**
 * @param plotheight plot height
 * @param plotwidth plot width
 * @param v values
 * @param max soft maximum
 * @param min hard minimum
 * @param n highest index of valid values in @p v
 * @param pc plot character
 * @param hce error character for max
 * @param lce error character for min
 * @param hm hard maximum
 */
void plot_values(const int plotheight,
                 const int plotwidth,
                 const double *v,
                 double max,
                 const double min,
                 const int n,
                 const char pc,
                 const char hce,
                 const char lce,
                 const double hm)
{
    // x screen coordinate
    int x = 0;
    // y screen coordinate of previous row
    int lasty = INT_MIN;
    max-=min;

#define D                                                               \
    if (v[i] == DOUBLE_MIN) {                                          \
      continue;                                                         \
    }                                                                   \
    const int y = (v[i]>=hm) ? plotheight : (v[i]<=min) ? 0 : (int)(((v[i]-min)/max)*(double)plotheight); \
    draw_line(x++, lasty, y, (v[i]>hm)  ? hce : (v[i]<min)  ? lce : pc); \
    lasty = y;

    for(int i = n+1; i < plotwidth; ++i)
    {
      D;
    }
    for(int i = 0; i <= n; ++i)
    {
      D;
    }
}

void resize(int sig) {
    (void) sig;
    endwin();
    refresh();
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

int main(int argc, char *argv[]) {
    const size_t values_len = 1024;
    double values1[values_len];
    double values2[values_len];
    for(size_t i = 0 ; i < values_len; ++i)
    {
      values1[i] = DOUBLE_MIN;
      values2[i] = DOUBLE_MIN;
    }
    double cval1=DOUBLE_MAX, pval1=DOUBLE_MAX;
    double cval2=DOUBLE_MAX, pval2=DOUBLE_MAX;
    double min1=DOUBLE_MAX, max1=DOUBLE_MIN, avg1=0;
    double min2=DOUBLE_MAX, max2=DOUBLE_MIN, avg2=0;
    int n=0;
    int r=0;
    int v=0;
    int screenwidth=0, screenheight=0;
    int plotwidth=0, plotheight=0;
    time_t t1,t2,td;
    struct tm *lt;
    int c;
    char plotchar1='1', plotchar2='2', max_errchar='e', min_errchar='v';
    double max = 0;
    double min = 0;
    double softmax=DOUBLE_MIN;
    double softmin=DOUBLE_MAX;
    double hardmax=DOUBLE_MAX;
    double hardmin = DOUBLE_MIN;
    const char *title = NULL;
    const char *unit = "";
    char ls[256]={0};
    int rate=0;
    int two=0;

    opterr=0;
    while((c=getopt(argc, argv, "2rc:e:E:s:S:m:M:t:u:")) != -1)
        switch(c) {
            case 'r':
                rate=1;
                break;
            case '2':
                two=1;
                break;
            case 'c':
                plotchar1=optarg[0];
                plotchar2=optarg[1];
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
                for(size_t i = 0; i < values_len; ++i)
                {
                    values1[i]=hardmin;
                    values2[i]=hardmin;
                }
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
    refresh();
    #ifdef NOGETMAXYX
    screenheight=LINES;
    screenwidth=COLS;
    #else
    getmaxyx(stdscr, screenheight, screenwidth);
    #endif
    mvprintw(screenheight/2, (screenwidth/2)-14, "waiting for data from stdin");
    refresh();

    while(1) {
        if(two)
            r=scanf("%lf %lf", &values1[n], &values2[n]);
        else
            r=scanf("%lf", &values1[n]);
        v++;
        if(r==0) {
            while(getchar()!='\n');
            continue;
        }
        else if(r<0) {
            break;
        }

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
        } else {
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
        if(plotwidth >= (int)(values_len - 1))
        {
          plotwidth = values_len - 1u;
        }

        getminmax(plotwidth, values1, &min1, &max1, &avg1, v);
        if (max1 > max)
        {
          max = max1;
        }
        if (min1 < min)
        {
          min = min1;
        }

        if (two)
        {
            getminmax(plotwidth, values2, &min2, &max2, &avg2, v);
            if (max2 > max)
            {
              max = max2;
            }
            if (min2 < min)
            {
              min = min2;
            }
        }

        if(max<softmax)
            max=softmax;
        if(hardmax!=DOUBLE_MAX)
            max=hardmax;
        if(softmin < min)
          min = softmin;
        if(hardmin != DOUBLE_MIN)
          min = hardmin;

        mvprintw(screenheight-1, screenwidth-sizeof(verstring)/sizeof(char), verstring);

        lt=localtime(&t1);
        #ifdef __sun
        asctime_r(lt, ls, sizeof(ls));
        #else
        asctime_r(lt, ls);
        #endif
        mvprintw(screenheight-2, screenwidth-strlen(ls), "%s", ls);

        mvprintw(screenheight-2, 5, "%c last=%.1f min=%.1f max=%.1f avg=%.1f %s", plotchar1, values1[n], min1, max1, avg1, unit);
        if(rate)
            printw(" interval=%llds", (long long int)td);

        if(two) {
            mvprintw(screenheight-1, 5, "%c last=%.1f min=%.1f max=%.1f avg=%.1f %s", plotchar2, values2[n], min2, max2, avg2, unit);
        }

        if (title)
        {
          mvprintw(0, (screenwidth/2)-(strlen(title)/2), "%s", title);
        }
        draw_axes(screenheight, plotheight, plotwidth, max, min, unit);
        plot_values(plotheight, plotwidth, values1, max, min, n, plotchar1, max_errchar, min_errchar, hardmax);
        if (two)
        {
          plot_values(plotheight, plotwidth, values2, max, min, n, plotchar2, max_errchar, min_errchar, hardmax);
        }

        if(n<(int)((plotwidth)-1))
            n++;
        else
            n=0;

        move(0,0);
        refresh();
    }

    endwin();
    delscreen(sp);
    return EXIT_SUCCESS;
}
