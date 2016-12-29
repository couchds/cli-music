/*
 *  CLI-music.c
 *  Music player accessible through CLI.
 *  Stores paths to MP3 files in MySQL database
 *  
 *  To do: implement nicer design pattern for windows...
 *  it's not very optimized now
 *  (read about panels for ncurses)
 *
 * */

/* Note: Database format is:
 *          
 *          +------------+--------------+------+-----+---------+-------+
 *          | Field      | Type         | Null | Key | Default | Extra |
 *          +------------+--------------+------+-----+---------+-------+
 *          | id         | int(11)      | YES  |     | NULL    |       |
 *          | path       | varchar(500) | YES  |     | NULL    |       |
 *          | song_name  | varchar(500) | NO   | PRI | NULL    |       |
 *          | artist     | varchar(500) | NO   | PRI | NULL    |       |
 *          | view_count | int(11)      | YES  |     | 0       |       |
 *          +------------+--------------+------+-----+---------+-------+
 *
 * Where id is the # of the recorded song, and path is the absolute
 * path to the directory where the mp3 file is stored.
 *  
 * Currently, the supported format for mp3 files is
 *  [Artist - Song.mp3]
 * Songs that do not have this format are just ignored.
 *
 * */


/* mm = main menu */


#include <dirent.h>
#include <fcntl.h>
#include <menu.h>
#include <mysql/mysql.h>
#include <ncurses.h>
#include <signal.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>


#define ENTER_KEY 10
#define SUCCESS 0
#define WINDOWS 12
#define UNIX 13
#define DISPLAY_COUNT 10
#define MAX_NUM_SONGS 1000
#define MAX_SONG_NAME_LENGTH 510

#ifdef OS_WINDOWS
#define MY_OS 12
#else
#define MY_OS 13
#endif

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))


/* Prototypes */
WINDOW *create_newwin(int height, int width, int starty, int startx);
int get_file_name(MYSQL *db_connection);
int load_into_database(MYSQL *db_connection, char *path);
int play_song(MYSQL *db_connection);
int process_main_menu(MYSQL *db_connection);
int song_menu(MYSQL *db_connection);
void finish_with_error(MYSQL *db_connection, int errnum);
char *get_song_name(MYSQL *db_connection, int id);
void free_memory(ITEM **items, MENU *menu_ptr, int num_items);
void update_view_count(MYSQL *db_connection, char *file_path);



const char *usage = "usage: mus username hostname password\n";

/* Options for main menu. */
const char *mm_options[] =
{
    "1.) Load music",
    "2.) Change song",
    "3.) Download song",
    "4.) Exit",
};


/* Function create_newwin
 * Create new window to be displayed
 * Will be used when I get around to optimizing it
 *
 * */
WINDOW *create_newwin(int height, int width, int starty, int startx)
{
    WINDOW *local_win;
    local_win = newwin(height, width, starty, startx);
    box(local_win, 0, 0);
    wrefresh(local_win);
    return local_win;
}


/* Function count_db_elements
 *  Returns how many elements are in the database.
 *  */
char *count_db_elements(MYSQL *db_connection)
{

    /* Get number of songs in database */
    const char COUNT_QUERY[] = "SELECT COUNT(*) FROM Songs";
    int query_result = mysql_query(db_connection, COUNT_QUERY);
    MYSQL_RES *count_res = mysql_store_result(db_connection);
    MYSQL_ROW count_row = mysql_fetch_row(count_res);
    mysql_free_result(count_res);
    char *count = count_row[0];
    /* Get single element in row, need to dereference it */
    return count;
}

/* Struct used to store two strings: song name and artist name */
struct song_info
{
    char artist[MAX_SONG_NAME_LENGTH];
    char song_name[MAX_SONG_NAME_LENGTH];
    char file_path[MAX_SONG_NAME_LENGTH];
};

/* Function get_file_info
 *  Process name of file, and
 *  retrieve name of artist and song.
 *  Returned in file_name_data struct
 *  */
struct song_info get_file_info(const char *file_name)
{
    struct song_info fdata;
    const char *delimiter = " - ";
    /* Have to use strlen here... each character SEEMS
     * to be 4 bytes (due to curses str???) but not sure */
    int file_name_length = strlen(file_name);
    /* Returns pointer to first occurence of delimiter,
     * where the artist name ends. */
    char *part2 = strstr(file_name, delimiter);
    /* Buffer for artist name */
    int artist_length = file_name_length - strlen(part2);
    char buff[artist_length];
    /* Everything from file_name to part2 is the artist name */
    strncpy(buff, file_name, artist_length);
    /* Null terminate */
    buff[artist_length] = 0x00;
    strncpy(fdata.artist, buff, artist_length); 
    /* strlen(" .  .mp3") = 7 */
    char buff2[1024];
    memcpy(buff2, &part2[3], strlen(part2-7));
    /* null terminate the string */
    buff2[strlen(part2-7)-3] = 0x00;
    strcpy(fdata.song_name, buff2);
    return fdata;
}


/* Function insert_song
 *  Insert a song into database, given path name.
 *  Also associates it with an ID, starting at 1
 *  for the first song.
 *
 * */
void insert_song(MYSQL *db_connection, char *abs_path, char *rel_path)
{   
    int query_status;
    int id = atoi(count_db_elements(db_connection));
    char query[255], exists_query[255];
    /* Next lines check if record exists in database,
     * and stores the result in the variable exist,
     * which is either 0 or 1. */

    /* First get song_name from path. */
    struct song_info fdata = get_file_info(rel_path);
    sprintf(exists_query, "SELECT EXISTS(SELECT * FROM Songs WHERE song_name='%s\')", fdata.song_name);
    query_status = mysql_query(db_connection, exists_query);
    if (query_status != SUCCESS)
        {
            printf("%s\n", exists_query);
            finish_with_error(db_connection, query_status);
        }
    MYSQL_RES *exists_res = mysql_store_result(db_connection);
    /* MYSQL_ROW is an array of pointers. 
     * Get the row and dereference the only thing in it, 0 or 1 */
    MYSQL_ROW exists_row = mysql_fetch_row(exists_res);
    char *exists = exists_row[0];
    /* exists == 0 indicates that no such record exists. 
     * So add it to the database. */
    if (atoi(exists) == 0)
    {
        /* Build query */
        sprintf(query, "INSERT INTO Songs (path, song_name, artist, id) VALUES (\"%s\", \"%s\", \"%s\", %d)", abs_path, fdata.song_name, fdata.artist, id);
        printf("%s\n", query);
        /* MP3 file => add to database */
        query_status = mysql_query(db_connection, query);
        if (query_status != SUCCESS)
        {
            finish_with_error(db_connection, query_status);
        }
    }
    else printf("Record already exists: %s\n", abs_path);
}

/* Function load_into_database
 *  Places path to file into database.
 *  Iterates over files in a directory,
 *  adds the absolute path to the file to
 *  the database if it is not in there.
 *
 *  */
int load_into_database(MYSQL *db_connection, char *path)
{
    /* Keeps track of error code from query */
    printf("Opening path... %s\n", path);
    DIR *dir;
    char buffer[255];
    /* Absolute path to file */
    char *real_path = realpath(path, buffer);
    struct dirent *ent;
    if ((dir = opendir(path)) != NULL)
    {
        /* Iterate over entries in directory */
        while ((ent = readdir(dir)) != NULL)
        {
            char *file_name = ent->d_name;
            /* Get everything to the right of the right-most . */
            char *end = strrchr(file_name, '.');
            if (end && !strcmp(end, ".mp3"))
            {
                char full_path[255];
                /* Build up full path to the mp3 file */
                strcpy(full_path, real_path);
                /* Path delimiter based on OS */
                if (MY_OS == UNIX) strcat(full_path, "/");
                if (MY_OS == WINDOWS) strcat(full_path, "\\\\");
                strcat(full_path, file_name);
                /* Place song in database */
                insert_song(db_connection, full_path, file_name);
            }
        }
    }
    closedir(dir);
    return SUCCESS;
}



/* Function play_song
 *  Plays a song
 *  */
int play_song(MYSQL *db_connection)
{
    return SUCCESS;
}



/* Function get_file_name
 *  Screen to prompt the user for a file name
 *  */
int get_file_name(MYSQL *db_connection)
{
    const char *PROMPT = "Enter path to directory with mp3 files.";
    int row, col;
    char dir[100];

    initscr();
    cbreak();
    noecho();

    getmaxyx(stdscr,row,col);
    mvprintw(row/2,(col-strlen(PROMPT))/2,"%s", PROMPT);

    getstr(dir);

    /* Now have path to directory where mp3 files should be.
     * Add them to the database. */
    load_into_database(db_connection, dir);    

    mvprintw(LINES-2, 0, "Any key to continue.");
    getch();
    endwin();

    /* Restore control to main menu */
    clear();
    process_main_menu(db_connection);
    return SUCCESS;
}


/* Function update_song_menu_options
 *  Get 10 songs from database to display
 *  Implementation can be improved later
 *
 * */
void update_song_menu_options(char song_options[][MAX_SONG_NAME_LENGTH])
{
    int i;
}



/* Function get_song_name
 *  Given an id,
 *  return struct w/ artist and song name
 *  */
struct song_info get_song_info(MYSQL *db_connection, int id)
{
    struct song_info sinfo;
    char *song_path;
    char query[255];
    sprintf(query, "SELECT song_name, artist, path FROM Songs WHERE id=%d", id);
    int query_result = mysql_query(db_connection, query);
    MYSQL_RES *path_res = mysql_store_result(db_connection);
    MYSQL_ROW path_row = mysql_fetch_row(path_res);
    mysql_free_result(path_res);
    strcpy(sinfo.song_name, path_row[0]);
    strcpy(sinfo.artist, path_row[1]);
    strcpy(sinfo.file_path, path_row[2]);
    return sinfo;
}


/* Function kill_proc
 *  End music playing
 *  */
void kill_proc(int pid)
{
    kill(pid, SIGTERM);
    /* Clear line */
    move(LINES - 2, 0);
    clrtoeol();
}



/* Function update_view_count
 *  Increment view_count of song in database
 *  */
void update_view_count(MYSQL *db_connection, char *file_path)
{
    char query[1024];
    printf("HERE\n");
    sprintf(query, "UPDATE Songs SET view_count = view_count + 1 WHERE path = %s", file_path);
    mysql_query(db_connection, query);
}

/* Function song_menu
 *  Display songs in database
 *  Get input on which song to play
 *  */
int song_menu(MYSQL *db_connection)
{
    int c, n_choices_song_menu, i, query_result;
    ITEM **song_menu_items;
    ITEM *cur_item;
    MENU *song_menu_ptr;

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);


    int db_size = atoi(count_db_elements(db_connection));
    /* If less than 10 elements, we display less than 10. */
    n_choices_song_menu = MIN(db_size, DISPLAY_COUNT);
    
    song_menu_items = (ITEM **)calloc(n_choices_song_menu+1, sizeof(ITEM *));

    char song_menu_song_names[MAX_NUM_SONGS][MAX_SONG_NAME_LENGTH];
    char song_menu_artist_names[MAX_NUM_SONGS][MAX_SONG_NAME_LENGTH];
    char song_menu_file_paths[MAX_NUM_SONGS][MAX_SONG_NAME_LENGTH];

    /* Copy first ten elements of database (sorted by ID) into menu options. */


    /* Begin by displaying the first 10 songs in the database. */
    for (i = 0; i < n_choices_song_menu; ++i)
    {
        struct song_info sinfo = get_song_info(db_connection, i);
        strcpy(song_menu_song_names[i], sinfo.song_name);
        strcpy(song_menu_artist_names[i], sinfo.artist);
        strcpy(song_menu_file_paths[i], sinfo.file_path);
    }
    for (i = 0; i < n_choices_song_menu; ++i)
    {
        song_menu_items[i] = new_item(song_menu_song_names[i], "");
    }
    
    song_menu_items[n_choices_song_menu] = (ITEM *) NULL;
    song_menu_ptr = new_menu((ITEM **) song_menu_items);
    
    mvprintw(LINES - 1, 5, "q to quit");
    post_menu(song_menu_ptr);
    refresh();

    /* Keep track of selected item - current_item wasn't working */
    int selected_item = 0;
    bool halt = false;
    /* Is a song playing? */
    bool playing = false;
    int pid = 1;
    /* Can refactor this loop */
    while (halt != true)
    {
        if (pid != 0) c = getch();
        if (c == 'q')
        {
            kill_proc(pid);
            break;
        }
        switch (c)
        {
            case (KEY_DOWN):
            {
                menu_driver(song_menu_ptr, REQ_DOWN_ITEM);
                if (selected_item < n_choices_song_menu-1) selected_item++;
                break;
            }
            case (KEY_UP):
            {
                menu_driver(song_menu_ptr, REQ_UP_ITEM);
                if (selected_item > 0) selected_item--;
                break;
            }
            case (ENTER_KEY):
                /* If no song is currently playing,
                 * spawn a new process and play a song. */
                if (playing == false)
                {
                    pid = fork();
                    if (pid < 0) exit(1);
                    /* child process */
                    else if (pid == 0)
                    {
                        /* write output to /dev/null */
                        int fd = open("/dev/null", O_WRONLY);
                        dup2(fd, 1);
                        dup2(fd, 2);
                        close(fd);
                        char arg[1024];
                        execl("/usr/bin/mpg123", \
                                "mpg123", \
                                song_menu_file_paths[selected_item],
                                (char *) 0); // sentinel value - end of parameter list
                        update_view_count(db_connection, song_menu_file_paths[selected_item]);
                        /* Exit when done playing song (unless it's signalled to die)*/
                        exit(1);

                    }
                    else
                    {
                        mvprintw(LINES - 2, .5, "Playing song: %s", song_menu_song_names[selected_item]);
                        refresh();
                    }
                    playing = true;
                }
                else
                {
                    kill_proc(pid);
                    playing = false;
                }
                refresh();
                break;
        }
    }
    /* free memory */
    free_memory(song_menu_items, song_menu_ptr, n_choices_song_menu);
    clear();
    /* return control to main menu */
    process_main_menu(db_connection);
    return SUCCESS;
}

/* Function free_mm
 *  Free memory of main manu
 *  */
void free_memory(ITEM **items, MENU *menu_ptr, int num_items)
{
    int i;
    printf("HERE\n");
    for (i = 0; i < num_items-2; i++) free(items[i]);
    free_menu(menu_ptr);
    printf("Reached here: %d\n", num_items);
}

/* Function process_main_menu
 *  Upon being called, render main menu to screen
 *
 *  */
int process_main_menu(MYSQL *db_connection)
{
    int c, n_choices_mm, i;
    ITEM **mm_items;
    ITEM *cur_item;
    MENU *mm_ptr;

    /* Set up Curses */
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);

    n_choices_mm = sizeof(mm_options)/sizeof(mm_options[0]);
    mm_items = (ITEM **)calloc(n_choices_mm+1, sizeof(ITEM *));

    for (i = 0; i < n_choices_mm; i++)
    {
        /* Populate array of items in main menu */
        mm_items[i] = new_item(mm_options[i], "");
    }
    
    
    mm_items[n_choices_mm] = (ITEM *) NULL;
    mm_ptr = new_menu((ITEM **) mm_items);

    post_menu(mm_ptr);
    refresh();
    /* Keep track of selected item - current_item wasn't working */
    int selected_item = 0;
    bool halt = false;
    /* Get user input until 'q' is pressed */
    while (halt != true)
    {
        if (halt != true) c = getch();
        switch(c)
        {
            case (KEY_DOWN):
                {
                    menu_driver(mm_ptr, REQ_DOWN_ITEM);
                    if (selected_item < n_choices_mm-1) selected_item++;
                    break;
                }
            case (KEY_UP):
                {
                    menu_driver(mm_ptr, REQ_UP_ITEM);
                    if (selected_item > 0) selected_item--;
                    break;
                }
            case (ENTER_KEY):
                switch (selected_item)
                {
                    /* Load music into database */
                    case (0):
                        //free_memory(mm_items, mm_ptr, n_choices_mm);
                        //endwin();
                        clear();
                        get_file_name(db_connection);
                        break;

                        /* Control goes to song menu */
                    case (1):
                        //free_memory(mm_items, mm_ptr, n_choices_mm);
                        //endwin();
                        clear();
                        song_menu(db_connection);
                        break;

                    case (2):
                        break;
                   
                       /* Download music */ 
                    case (3):
                        free_memory(mm_items, mm_ptr, n_choices_mm);
                        endwin();
                        return SUCCESS;

                }
                break;
        }
    }
    free_memory(mm_items, mm_ptr, n_choices_mm);
    //refresh();
    endwin();
    return SUCCESS;
}



/* Function finish_with_error
 * Error at some point in MySQL C Connector
 * => Print to stderr, exit
 * */
void finish_with_error(MYSQL *db_connection, int errnum)
{
    const char *error_message = mysql_error(db_connection);
    /* Check is there is an error with the actual connection */
    if (error_message[0])
    {
        fprintf(stderr, "%s\n", error_message);
    }
    /* Otherwise print the error associated with the error code */
    if (errnum != SUCCESS)
    {
        fprintf(stderr, "%s\n", strerror(errnum));
    }
    exit(1);
}

int main(int argc, char *argv[])
{
    if (argc != 4)
    {
        fprintf(stderr, "%s\n", usage);
        exit(1);
    }
    char *username = argv[1];
    char *hostname = argv[2];
    char *password = argv[3];
    /* Current name of database where music is */
    const char *DBNAME = "music";
    MYSQL *conn = mysql_init(NULL);
    if (mysql_real_connect(conn, hostname, username, password, DBNAME, 0, NULL, 0) == NULL)
    {
        finish_with_error(conn, SUCCESS);
    }
    /* At this point, make sure the database is formatted correctly.
     * That is, make sure there is an element with ID=1, and there are
     * no discontinuities in the ID's
     *  Also, make sure that all the files actually exist - if they don't,
     *  remove them from the database.
     *
     * . TO BE IMPLEMENTED*/

    process_main_menu(conn);
    mysql_close(conn);
    return SUCCESS;
}
