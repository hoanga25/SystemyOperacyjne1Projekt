/*
------------------------------------------------------------------------
Oświadczam, że niniejsza praca stanowiąca podstawę do uznania
osiągnięcia efektów uczenia się z przedmiotu SOP została wykonana przeze
mnie samodzielnie.
[Anna Hoang]
------------------------------------------------------------------------
*/

#define _XOPEN_SOURCE 500
#define _POSIX_C_SOURCE 200112L
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <ftw.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <fcntl.h>

#define MaxParts 8
#define MaxL 5000
#define MaxC 5
#define MaxStrlen 1000
#define MaxLine 10000

#define ERR(source) (perror(source),\
		     fprintf(stderr,"%s:%d\n",__FILE__,__LINE__),\
		     exit(EXIT_FAILURE))

#define ELAPSED(start,end) ((end).tv_sec-(start).tv_sec)+(((end).tv_nsec - (start).tv_nsec) * 1.0e-9)

typedef unsigned int UINT;
typedef struct timespec timespec_t;


typedef struct part{
    /* wspoldzielone z maze */
    pthread_t main_t;
    int L,c;
    
    pthread_mutex_t wait_m;
    int wait; /* do SIGINT SIGQUIT */
    sigset_t sigusr,intquit;
    /*-----------------------------*/

    /* watek-uczestnik */
    pthread_t tid;
    int id;

    char nick[MaxStrlen];

    unsigned int seed; /* ziarno do losowania v i d */
    double v; /* predk */

    pthread_mutex_t clock_m;
    double t,d; /* mierzenie czasu,dystansu */
    timespec_t start,current;
    
    pthread_mutex_t signo_m,print_m;
    int c_fin; /* ile ukonczonych okrazen */
    int printed_table; /* info czy wypisano tablice tego uczestnika */
    
    /* pomocnicze */
    double d_tmp,d_min,d_max;
}part;

typedef struct maze{
    /* wspoldzielone z part */
    pthread_t main_t,s_t;
    int n,L,c;

    pthread_mutex_t wait_m;
    int wait; /* do SIGINT SIGQUIT */
    /*-----------------------------*/
    int st;
    part part[MaxParts];
    pthread_t menu_t,kom_t; /* menu glowne i menu gry */
    sigset_t sigusr,intquit; /* zbiory sygnalow */
    
    pthread_mutex_t unprinted_m, sort_m, print_m;

    int sig1count; /* licznik SIGUSR1 */
    
    char* res_path, *nick_path, *res_txt;
}maze;

/*---------------------------------------------------*/
void prep_args (int argc, char **argv, maze* wyscig);
void prep0 (maze* w);
void prep_v (maze* w);

void info (void *arg);


/*-----------------w main-----------------*/
void* signal_handling(void*);

/*-------------w menu glownym-------------*/
void prep_nicks_gen (maze* w);
void prep_nicks_stdin (maze* w);
void prep_nicks_file (char*,maze* w);

void add_part(maze*);
void* p_thread(part*);
void* komends(void*);
/*----------------------------------------*/
void* menu_main(void *);

void p_handler (void *a);

void exit_safe(maze* w);
int maze_fin(maze*);
void fin_wait(maze*,int);

void print_tables(maze*);
void part_table(part*);

int* posort_i(maze*);
void doubleS(char*, double);
void no_endl(char*,char*);
ssize_t bulk_write(int fd, char *buf, size_t count);
ssize_t bulk_read(int fd, char *buf, size_t count);

void results(maze*);
void results_file(maze*,char*);

int main(int argc, char **argv){
    srand(time(NULL));
    maze w;
    prep_args(argc,argv,&w);
    prep0(&w);
    prep_v(&w);
    
    /*--------TWORZENIE WATKU OBSLUGUJACEGO SIGINT I SIGQUIT---------------------------*/
    sigemptyset(&w.intquit);
    sigaddset(&w.intquit, SIGINT);
    sigaddset(&w.intquit, SIGQUIT);
    pthread_sigmask(SIG_BLOCK, &w.intquit, NULL);
    if (pthread_create(&w.s_t, NULL,signal_handling, &w)) ERR("pthread_create");
    
    /*--------TWORZENIE MENU GLOWNEGO--------------------------------------------------*/
    sigemptyset(&w.sigusr);
    sigaddset(&w.sigusr, SIGUSR1);
    sigaddset(&w.sigusr, SIGUSR2);
    pthread_sigmask(SIG_BLOCK, &w.sigusr, NULL);
    if (pthread_create(&w.menu_t,NULL,menu_main, &w)) ERR("pthread_create");
    
    /*--------CZEKANIE NA SYGNALY-----------------------------------------------------*/
    int signo;
    w.sig1count = 0;
    while(w.sig1count<MaxParts*MaxC){
		if(!sigwait(&w.sigusr, &signo)) {
            if (signo==SIGUSR1) {
                w.sig1count++;
                print_tables(&w);
                if (maze_fin(&w)==1) {
                    printf("Maze finished\n");
                    results(&w);
                    if (w.res_path){
                        results_file(&w,w.res_path);
                        printf("Printed in %s as well\n",w.res_path);
                    }
                    exit_safe(&w);
                }
            }
            else if (signo==SIGUSR2){
                printf("Maze requested to finish\n");
                exit_safe(&w);
            }
        }
	}
    return 0;
}

void prep_nicks_gen (maze* wyscig){
    char nr = '1';
    char* name = "nick";
    for (int i=0;i<MaxParts;i++){
        strcpy(wyscig->part[i].nick,name);
        strncat(wyscig->part[i].nick,&nr,1);
        nr++;
    }
}
void prep_nicks_stdin (maze* wyscig){
    char buf[MaxStrlen]; int skipped=-1;
    printf("Enter %d names below:\n",wyscig->n);
    for (int i=0;i<wyscig->n;i++){
        if (!wyscig->wait) fgets(buf,MaxStrlen,stdin);
        if (!strcmp(buf,"y\n") && wyscig->wait) exit_safe(wyscig);
        else if (wyscig->wait && !strcmp(buf,"n\n")) {
            printf("re-enter [y/n]\n");
            skipped=i;
        }
        while (wyscig->wait) {
            int signo;
            if (!sigwait(&wyscig->sigusr,&signo)){
                if (signo==SIGUSR2 && !wyscig->wait) {
                    break;
                }
            } 
            else ERR("sigwait");    
        }
        if (skipped!=-1 && !wyscig->wait) {
            printf("re-renter nick %d\n",skipped+1);
            fgets(buf,MaxStrlen,stdin);
        }
        if (!wyscig->wait) no_endl(wyscig->part[i].nick,buf);
    }
}
void prep_nicks_file (char* path, maze* wyscig){
    ssize_t count; int in;
    if ((in=TEMP_FAILURE_RETRY(open(path,O_RDONLY)))<0) ERR("open");

    struct stat st;
    stat(path, &st);
    int size = st.st_size;
    int b = ceil((double)size/MaxLine);
    
    char *tekst = malloc((1+size)*sizeof(char));
    if (!tekst) ERR("malloc");
    
    for (int i=0;i<b;i++){
        if((count=bulk_read(in,tekst,size)<0)) ERR("read");
    }
    if(TEMP_FAILURE_RETRY(close(in)))ERR("close");
    tekst[size]='\0';
    
    int n = 0;
    char* line = strtok (tekst,"\n");
    while (line != NULL){
        strcpy(wyscig->part[n].nick,line);
        line = strtok (NULL,"\n");
        n++;
    }
    wyscig->n = n;
    free(tekst);
}
void prep_args (int argc, char **argv, maze* wyscig){
    if (argc<2) ERR("Too few parameters.");
    char c;

    wyscig->n = 0;
    wyscig->c = 1;
    wyscig->nick_path = NULL;
    wyscig->res_path = NULL;

    int n_pres=0,p_pres=0;
    
    while ((c = getopt(argc, argv, "n:p:l:o:f:")) != -1){
        switch (c)
        {
        case 'n':
            if (p_pres) ERR("Participants have their nicknames already");
            else if (atoi(optarg)<2 || atoi(optarg)>8) ERR("Wrong parameter -n.");
            else {
                wyscig->n = atoi(optarg);
                n_pres = 1;
            }
            break;
        case 'p':
            if (n_pres) ERR("Participants have their nicknames already");
            else {
                wyscig->nick_path=optarg;
                prep_nicks_file(optarg,wyscig);
                p_pres=1;
            }
            break;
        case 'l':
            if (atoi(optarg)>5000 || atoi(optarg)<100) ERR("Wrong parameter -l.");
            wyscig->L = atoi(optarg);
            break;
        case 'o':
            if (atoi(optarg)>5) ERR("Wrong parameter -o.");
            else wyscig->c = atoi(optarg);
            break;
        case 'f':
            wyscig->res_path=optarg;
            break;
        default:
            ERR("Unknown parameter");
            break;
        }
    }

}
void prep0 (maze* w){
    char *usr = getenv("USER");
    printf("Hello %s!\n", usr);
    if (!w->nick_path) printf("Do you want to name the participants? [y/n]\n");
    w->main_t = pthread_self();
    pthread_mutex_init(&w->wait_m,NULL);
    pthread_mutex_init(&w->unprinted_m,NULL);
    pthread_mutex_init(&w->sort_m,NULL);
    pthread_mutex_init(&w->print_m,NULL);
    
    w->wait = 0;
    w->st = 0;
    
    /*--------------------uczestnicy---------------------*/
    for (int i=0;i<MaxParts;i++){
        w->part[i].main_t = w->main_t;
        w->part[i].sigusr = w->sigusr;

        w->part[i].L = w->L;
        w->part[i].c = w->c;
        pthread_mutex_init(&w->part[i].wait_m,NULL);
        w->part[i].wait = 0;
        /*----------------------------*/
        w->part[i].id = i+1;

        w->part[i].seed = rand()+1;
        w->part[i].v = 0;

        pthread_mutex_init(&w->part[i].clock_m,NULL);
        pthread_mutex_init(&w->part[i].signo_m,NULL);
        w->part[i].t = 0;
        w->part[i].d = 0;

        pthread_mutex_init(&w->part[i].print_m,NULL);
        w->part[i].c_fin = 0;
        w->part[i].printed_table = 1;

        w->part[i].d_tmp = 0;
        w->part[i].d_min = 0;
        w->part[i].d_max = 0;
    }

    fin_wait(w,0);
}
void prep_v (maze* wyscig){
    double los;
    int n = wyscig->n;
    for (int i=0;i<n;i++){
        los = rand_r(&(wyscig->part[i].seed))%1100;
        los = ((los + 9500)/10000)*wyscig->L/10;
        wyscig->part[i].v = los;
    }
}

void* signal_handling(void* a) {
    maze* t = (maze*) a;
	int signo;
	for (;;) {
		if(!sigwait(&t->intquit, &signo)) {
            if (signo == SIGINT || signo == SIGQUIT){
                fin_wait(t,1);
                printf("\nDo you want to finish?[y/n]\n");
                char d[MaxStrlen];
                fgets(d,MaxStrlen,stdin);
                if (!strcmp(d,"y\n")) exit_safe(t);
                if (!strcmp(d,"n\n")) fin_wait(t,0);
                
                if (pthread_kill(t->menu_t,SIGUSR2)) ERR("phtread_kill"); 
                
                if (maze_fin(t)>-1) {
                    pthread_kill(t->kom_t,SIGUSR2);
                    for (int i=0;i<t->n;i++) pthread_kill(t->part[i].tid,SIGUSR2);
                }
            }
        }
	}
    return NULL;
}

void* menu_main(void *a){
    maze* t = (maze*) a;
    char yn[MaxStrlen];

    if (!t->wait) fgets(yn,MaxStrlen,stdin);
    if (!strcmp(yn,"y\n")) {
        if (!t->wait) prep_nicks_stdin(t);
        else exit_safe(t);
    }
    else if (!strcmp(yn,"n\n") && !t->wait) prep_nicks_gen(t);
    else if (!strcmp(yn,"n\n") && t->wait) printf("re-enter [y/n]\n");
    while (t->wait) {
        int signo;
        if (!sigwait(&t->sigusr,&signo)){
            if (signo==SIGUSR2 && !t->wait) {
                break;
            }
        } 
        else ERR("sigwait");    
    }

    char komenda[MaxStrlen];
    while (1){
        while (maze_fin(t)!=1){
            printf("\n----------PRE-MAZE COMMAND--------\n");
            printf(">>");
            if (!t->wait) fgets(komenda,MaxStrlen,stdin);
            if (strlen(komenda)>=2){
                if(!strcmp("y\n",komenda)){
                    if (t->wait) exit_safe(t);
                    else {
                        printf("Wrong command\n");
                        continue;
                    }
                }
                else if (!strcmp("n\n",komenda)){
                    if (t->wait) fin_wait(t,0);
                    else printf("Wrong command\n");
                    continue;
                }
                
                else if (!strcmp("start\n",komenda)){
                    t->st=1;

                    for (int i=0;i<t->n;i++){
                        pthread_create(&t->part[i].tid,NULL,(void*)p_thread,&t->part[i]);
                    }

                    pthread_create(&t->kom_t,NULL,komends,a);
                    pthread_join(t->kom_t,NULL);
                    
                    for (int i=0;i<t->n;i++){
                        pthread_join(t->part[i].tid,NULL);
                    }
                    break;
                }
                else if (!strcmp("buffer\n",komenda)){
                    printf("\nDo you want to finish? [y/n]\n");
                }
                else if (!strcmp("info\n",komenda)) {
                    info(t);
                    continue;
                }
                else if (!strcmp("add_participant\n",komenda)){
                    if (t->n<MaxParts) {
                        add_part(t);
                    }
                    else printf("No change, there would be too many participants\n");
                    continue;
                }
                else if (!strcmp("change_laps\n",komenda)){
                    int laps; char linia[MaxStrlen];
                    printf("Change number of laps to:\n");
                    fgets(linia,MaxStrlen,stdin); laps = atoi(linia);
                    if (laps<=MaxC && laps>=1) t->c = laps;
                    else printf("No change, there would be too many laps\n");
                    continue;
                }
                else if (!strcmp("change_length\n",komenda)){
                    int length; char linia[MaxStrlen];
                    printf("Change maze length to:\n");
                    fgets(linia,MaxStrlen,stdin); length = atoi(linia);
                    if (length<=MaxL && length>=100) t->L = length;
                    else printf("No change, the maze would be too long\n");
                    continue;
                }
                else if (!strcmp("exit\n",komenda)) exit_safe(t);
                else {
                    printf("Wrong command\n");
                    continue;
                }
            }
        }
        while (t->wait) {
            int signo;
            if (!sigwait(&t->sigusr,&signo)){
                if (signo==SIGUSR2 && !t->wait) {
                    
                    break;
                }
            } 
            else ERR("sigwait");    
        }
        continue;
    }

    pthread_join(t->s_t,NULL);
    pthread_join(t->main_t,NULL);
    return NULL;
}

void add_part(maze *wyscig){
    int i = wyscig->n; wyscig->n++;
    printf("Enter this participants nickname:\n");
    char nick[MaxStrlen],buf;
    fgets(nick,MaxStrlen,stdin);
    no_endl(wyscig->part[i].nick,nick);

    double los = rand_r(&(wyscig->part[i].seed))%1100;
    los = ((los + 9500)/10000)*wyscig->L/10;
    wyscig->part[i].v = los;
}

void* p_thread(part* a){
    pthread_t main_t = a->main_t;
    a->d_max = 1.1*a->v;
    a->d_min = 0.9*a->v;

    while (a->c_fin<a->c){
        if (!a->wait){
            pthread_mutex_lock(&a->clock_m);
            if (clock_gettime(CLOCK_REALTIME, &a->start)) ERR("clock_gettime");
            while (a->d <= a->L){
                a->d_tmp = 1000*a->d_min + a->L*(rand_r(&a->seed))%200;
                a->d_tmp/=1000;
                a->d += a->d_tmp;
                sleep(1);
            }
            if (clock_gettime(CLOCK_REALTIME, &a->current)) ERR("clock_gettime");
            a->t+=ELAPSED(a->start,a->current);
            pthread_mutex_unlock(&a->clock_m);
            a->c_fin++;
            a->printed_table=0;

            printf("Wysłane do [%ld]\n",main_t);
            pthread_kill(main_t, SIGUSR1);
        }
        else{
            pthread_mutex_lock(&a->signo_m);
            while(a->wait){
                int signo;
                if (!sigwait(&a->sigusr,&signo)){
                    if (signo==SIGUSR2 && !a->wait) {
                        
                        break;
                    }
                } 
                else ERR("sigwait");
            }
            pthread_mutex_unlock(&a->signo_m);
        }
    }
    return NULL;
}

void* komends(void* a){
    maze* t = (maze*) a;
    char komenda[100];

    while (maze_fin(t)==0){
        while (!t->wait){
            printf("\n----------COMMAND DURING MAZE--------\n");
            printf(">>");
            if (!t->wait) fgets(komenda,MaxStrlen,stdin);
            if (strlen(komenda)>=2){
                if(!strcmp("y\n",komenda)){
                    if (t->wait) exit_safe(t);
                    else {
                        printf("Wrong command\n");
                        continue;
                    }
                }
                else if (!strcmp("n\n",komenda)){
                    if (t->wait) fin_wait(t,0);
                    else printf("Wrong command\n");
                    continue;
                }
                else if (!strcmp("exit\n",komenda)) exit_safe(t);
                else if (!strcmp("info\n",komenda)) info(t);
                else if (!strcmp("cancel\n",komenda)) pthread_kill(t->main_t,SIGUSR2);
                else if (!strcmp(komenda,"fault\n")) {
                    printf("Which participant will get a fault:\n");
                    char inp[MaxStrlen],nick[MaxStrlen];
                    if (!t->wait) {
                        fgets(inp,MaxStrlen,stdin); no_endl(nick,inp);
                        double drop = (10+rand()%21);
                        drop/=100;
                        for (int i=0;i<t->n;i++){
                            if (!strncmp(t->part[i].nick,nick,strlen(nick))){
                                printf("%s ran %f[m/s]\n",nick,t->part[i].v);
                                t->part[i].v*=(1-drop);
                                printf("%s now runs %f[m/s]\n",nick,t->part[i].v);
                                break;
                            }
                        }
                    }
                }
                else if (!strcmp(komenda,"cheat\n")) {
                    printf("Which participant will cheat:\n");
                    char inp[MaxStrlen],nick[MaxStrlen];
                    if (!t->wait) {
                        fgets(inp,MaxStrlen,stdin); no_endl(nick,inp);
                        for (int i=0;i<t->n;i++){
                            if (!strncmp(t->part[i].nick,nick,strlen(nick))){
                                printf("%s ran %f[m/s]\n",nick,t->part[i].v);
                                t->part[i].v*=5;
                                printf("%s now runs %f[m/s]\n",nick,t->part[i].v);
                                break;
                            }
                        }
                    }
                }
                else if (!strcmp(komenda,"results\n")) results(t);
                else if (!strcmp(komenda,"dropout\n")){
                    printf("Which participant will dropout:\n");
                    char inp[MaxStrlen],nick[MaxStrlen];
                    if (!t->wait){
                        fgets(inp,MaxStrlen,stdin); no_endl(nick,inp);
                        for (int i=0;i<t->n;i++){
                            if (!strncmp(t->part[i].nick,nick,strlen(nick))){
                                printf("Bye, %s :(\n",nick);
                                strncat(t->part[i].nick," [x]",4);
                                t->part[i].c_fin = t->c;
                                pthread_cancel(t->part[i].tid);
                                printf("[%ld]\n",t->part[i].tid);
                                break;
                            }
                        }
                    }
                }
                else {
                    printf("Wrong command\n");
                    continue;
                }
            }
        }
        while (t->wait) {
            int signo;
            if (!sigwait(&t->sigusr,&signo)){
                if (signo==SIGUSR2 && !t->wait) {
                    
                    break;
                }
            } 
            else ERR("sigwait");
        }
    }
    pthread_join(t->s_t,NULL);
    pthread_join(t->menu_t,NULL);
    
    return NULL;
}

int maze_fin(maze *t){
    int wynik=0;
    if (!t->st) wynik=-1;
    int finished=0;
    for (int i=0;i<t->n;i++){
        if (t->part[i].c_fin == t->part[i].c) finished++;
    }
    if (finished==t->n) wynik=1;
    return wynik;
}

void fin_wait(maze *t,int tak){
    pthread_mutex_lock(&t->wait_m);
    t->wait=tak;
    
    for (int i=0;i<t->n;i++){
        pthread_mutex_lock(&t->part[i].wait_m);
        t->part[i].wait=tak;
    }

    pthread_mutex_unlock(&t->wait_m);
    for (int i=0;i<t->n;i++){
        pthread_mutex_unlock(&t->part[i].wait_m);
    }
}

void info (void *arg){
    maze* wyscig = (maze*) arg;
    printf("\n\n--------------info------------\n");
    printf("Wyscig ma:\n");
    printf("- stan {%d}\n",maze_fin(wyscig));
    printf("- %d uczestnikow\n",wyscig->n);
    printf("- %d okrazen\n",wyscig->c);
    printf("- trase dlugosci %d\n\n",wyscig->L);

    printf("Jego uczestnicy to:\n");
    for (int i=0;i<wyscig->n;i++){
        printf("%d. %s\n",wyscig->part[i].id,wyscig->part[i].nick);
        printf("- predkosc %f [m/s]\n",wyscig->part[i].v);
        printf("- pokonany dystans %f [m]\n",wyscig->part[i].d);
    }
    printf("\n-------------------------------\n\n");
}

void print_tables(maze* t){
    pthread_mutex_lock(&t->unprinted_m);
    int unprinted=0;
    for (int i=0;i<t->n;i++){
        if (!t->part[i].printed_table &&
            t->part[i].c_fin == t->part[i].c) {
            unprinted++;
            part_table(&t->part[i]);
            t->part[i].printed_table=1;
        }
    }
    pthread_mutex_unlock(&t->unprinted_m);
}
void part_table(part* ucz){
    pthread_mutex_lock(&ucz->print_m);
    printf("\n\n-----------uczestnik-----------\n");
    printf("tid[%ld]\n",ucz->tid);
    printf("%d. %s\n",ucz->id,ucz->nick);
    printf("- predkosc %f [m/s]\n",ucz->v);
    printf("- pokonany dystans %f [m]\n",ucz->d);
    printf("- czas po ktorym pokonal ten dystans %f [s]\n",ucz->t);
    printf("\n-------------------------------\n\n");
    pthread_mutex_unlock(&ucz->print_m);
}

int* posort_i (maze* wyscig){
    pthread_mutex_lock(&wyscig->sort_m);
    int* ind = malloc(sizeof(int)*wyscig->n);
    double* t = malloc(sizeof(double)*wyscig->n);

    for (int j=0;j<wyscig->n;j++) {
        t[j]=wyscig->part[j].t;
        ind[j]=j;
    }
    
    for (int j=0;j<wyscig->n;j++){
        for (int k=0;k<wyscig->n;k++){
            if (k!=j && t[k] > t[j]){
                double tmp = t[k];
                t[k] = t[j];
                t[j] = tmp;
                int tmp2 = ind[j];
                ind[j] = ind[k];
                ind[k] = tmp2;
            }
        }
    }
    free(t);
    pthread_mutex_unlock(&wyscig->sort_m);
    return ind;
}

void doubleS (char* dest, double a){
    int przed=0;
    double t=a;
    while (t>=1){
        t/=10;
        przed++;
    }
    gcvt(a,przed+3,dest);
}

void no_endl(char* dest,char* source){
    strcpy(dest,source); dest[strlen(dest)-1]='\0';
}

void results(maze* wyscig){
    pthread_mutex_lock(&wyscig->print_m);
    int* ind = posort_i(wyscig);
    printf("\n\n---------tabela wynikow---------\n");
    for (int j=0;j<wyscig->n;j++){
        int i = ind[j];
        printf("%d. %s\n",wyscig->part[i].id,wyscig->part[i].nick);
        printf("- pokonany dystans %3f [m]\n",wyscig->part[i].d);
        printf("- czas po ktorym pokonal ten dystans %f [s]\n",wyscig->part[i].t);
    }
    printf("\n-------------------------------\n\n");
    free(ind);
    pthread_mutex_unlock(&wyscig->print_m);
}

void results_file(maze* wyscig, char* path){
    int out; ssize_t count;
    if((out=TEMP_FAILURE_RETRY(open(path,O_WRONLY | O_CREAT | O_TRUNC, 0644)))<0)ERR("open");
    int* ind = posort_i(wyscig);
    char* pocz = "\n\n---------tabela wynikow---------\n";
    char id[MaxStrlen];
    char* nick;
    char* dyst = "- pokonany dystans ";
    char d[MaxStrlen] ;
    char* czas = "- czas po ktorym pokonal ten dystans ";
    char t[MaxStrlen];
    char* kon = "\n-------------------------------\n\n";
    char line[1000];

    if((count=bulk_write(out,pocz,strlen(pocz)))<0) ERR("write");
    for (int j=0;j<wyscig->n;j++){
        int i = ind[j];
        doubleS(id,wyscig->part[i].id);
        nick = wyscig->part[i].nick;
        doubleS(d,wyscig->part[i].d);
        doubleS(t,wyscig->part[i].t);

        sprintf(line,"%s. %s\n",id,nick);
        if((count=bulk_write(out,line,strlen(line)))<0) ERR("write");
        sprintf(line,"%s %s[m]\n",dyst,d);
        if((count=bulk_write(out,line,strlen(line)))<0) ERR("write");
        sprintf(line,"%s %s[t]\n",czas,t);
        if((count=bulk_write(out,line,strlen(line)))<0) ERR("write");
    }
    if((count=bulk_write(out,kon,strlen(kon)))<0) ERR("write");
    if(TEMP_FAILURE_RETRY(close(out)))ERR("close");

    free(ind);
}

void p_handler (void *arg){
    part* a = (part*) arg;
    a->d -= a->d_tmp;
}

void exit_safe(maze* w){
    if (!w->st) {
        exit(EXIT_SUCCESS);
    }

    if (maze_fin(w)>-1){
        for (int i=0;i<w->n;i++){
            pthread_cancel(w->part[i].tid);
        }
        pthread_cancel(w->kom_t);
    }
    pthread_cancel(w->menu_t);

    if (maze_fin(w)>-1){
        for (int i=0;i<w->n;i++){
            pthread_join(w->part[i].tid,NULL);
        }
        pthread_join(w->kom_t,NULL);
    }
    pthread_join(w->menu_t,NULL);

    exit(EXIT_SUCCESS);
}

ssize_t bulk_read(int fd, char *buf, size_t count){
    ssize_t c;
    ssize_t len=0;
    do{
            c=TEMP_FAILURE_RETRY(read(fd,buf,count));
            if(c<0) return c;
            if(c==0) return len; //EOF
            buf+=c;
            len+=c;
            count-=c;
    }while(count>0);
    return len ;
}
ssize_t bulk_write(int fd, char *buf, size_t count){
    ssize_t c;
    ssize_t len=0;
    do{
            c=TEMP_FAILURE_RETRY(write(fd,buf,count));
            if(c<0) return c;
            buf+=c;
            len+=c;
            count-=c;
    }while(count>0);
    return len ;
}
