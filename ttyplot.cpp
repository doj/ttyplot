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

void draw_axes(const int h,
               const int ph,
               const int pw,
               const double max,
               const double min,
               const char *unit)
{
    mvhline(h-3, 2, T_HLINE, pw);
    mvvline(2, 2, T_VLINE, ph);
    mvprintw(1, 4, "%.1f %s", max, unit);
    mvprintw((ph/4)+1, 4, "%.1f %s", min/4 + max*3/4, unit);
    mvprintw((ph/2)+1, 4, "%.1f %s", min/2 + max/2, unit);
    mvprintw((ph*3/4)+1, 4, "%.1f %s", min*3/4 + max/4, unit);
    mvprintw(ph+1, 4, "%.1f %s", min, unit);
    mvaddch(h-3, 2+pw, T_RARR);
    mvaddch(1, 2, T_UARR);
    mvaddch(h-3, 2, T_LLCR);
}

void draw_line(const int x,
               const int ph,
               const int l1,
               const int l2,
               const char c1,
               const char c2,
               const char hce,
               const char lce)
{
#if 0
    if(l1 > l2) {
        mvvline(ph+1-l1, x, c1, l1-l2 );
        mvvline(ph+1-l2, x, c2|A_REVERSE, l2 );
    } else if(l1 < l2) {
        mvvline(ph+1-l2, x, (c2==hce || c2==lce) ? c2|A_REVERSE : ' '|A_REVERSE,  l2-l1 );
        mvvline(ph+1-l1, x, c1|A_REVERSE, l1 );
    } else {
        mvvline(ph+1-l2, x, c2|A_REVERSE, l2 );
    }
#else
    mvaddch(ph+1-l1,x,c1);
    mvaddch(ph+1-l2,x,c2);
#endif
}

/**
 * @param ph plot height
 * @param pw plot width
 * @param v1 values
 * @param v2 values
 * @param max soft maximum
 * @param min hard minimum
 * @param n highest index of valid values in @p v1 and @p v2
 * @param pc1 plot character for @p v1
 * @param pc2 plot character for @p v2
 * @param hce error character for max
 * @param lce error character for min
 * @param hm hard maximum
 */
void plot_values(const int ph,
                 const int pw,
                 const double *v1,
                 const double *v2,
                 double max,
                 const double min,
                 const int n,
                 const char pc1,
                 const char pc2,
                 const char hce,
                 const char lce,
                 const double hm)
{
    // x screen coordinate
    int x=3;
    max-=min;

#define D                                                               \
    if (v1[i] == DOUBLE_MIN) {                                          \
      continue;                                                         \
    }                                                                   \
    draw_line(x++, ph,                                                  \
      (v1[i]>=hm) ? ph  : (v1[i]<=min) ?  0  : (int)(((v1[i]-min)/max)*(double)ph), \
      (v2[i]>=hm) ? ph  : (v2[i]<=min) ?  0  : (int)(((v2[i]-min)/max)*(double)ph), \
      (v1[i]>hm)  ? hce : (v1[i]<min)  ? lce : pc1, \
      (v2[i]>hm)  ? hce : (v2[i]<min)  ? lce : pc2, \
      hce, lce)
    for(int i = n+1; i < pw; ++i)
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
    int width=0, height=0;
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
    char title[256]=".: ttyplot :.";
    char unit[64]={0};
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
                snprintf(title, sizeof(title), "%s", optarg);
                break;
            case 'u':
                snprintf(unit, sizeof(unit), "%s", optarg);
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
    height=LINES;
    width=COLS;
    #else
    getmaxyx(stdscr, height, width);
    #endif
    mvprintw(height/2, (width/2)-14, "waiting for data from stdin");
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
        height=LINES;
        width=COLS;
        #else
        getmaxyx(stdscr, height, width);
        #endif
        plotheight=height-4;
        plotwidth=width-4;
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

        mvprintw(height-1, width-sizeof(verstring)/sizeof(char), verstring);

        lt=localtime(&t1);
        #ifdef __sun
        asctime_r(lt, ls, sizeof(ls));
        #else
        asctime_r(lt, ls);
        #endif
        mvprintw(height-2, width-strlen(ls), "%s", ls);

        mvaddch(height-2, 5, plotchar1);
        mvprintw(height-2, 7, "last=%.1f min=%.1f max=%.1f avg=%.1f %s ",  values1[n], min1, max1, avg1, unit);
        if(rate)
            printw(" interval=%llds", (long long int)td);

        if(two) {
            mvaddch(height-1, 5, ' ');
            mvprintw(height-1, 7, "last=%.1f min=%.1f max=%.1f avg=%.1f %s   ",  values2[n], min2, max2, avg2, unit);
        }

        mvprintw(0, (width/2)-(strlen(title)/2), "%s", title);
        draw_axes(height, plotheight, plotwidth, max, min, unit);
        plot_values(plotheight, plotwidth, values1, values2, max, min, n, plotchar1, plotchar2, max_errchar, min_errchar, hardmax);

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
