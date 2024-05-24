#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <stdbool.h>
#include <stdarg.h>

#define MAX_PATH (5096)
#define ERROR_MSG "An error has occurred\n"
#define LOG false
#define logPrint(...) if (LOG) {fprintf(stderr, "[%*.*s]\t", 12, 12, __func__); fprintf(stderr, __VA_ARGS__);}

char SEARCH_PATH[MAX_PATH] = "/bin";
char HISTORY[100][5096];
int N_HISTORY_ENTRIES = 0;

typedef struct {
  int nargs;
  int pid;
  bool executed;
  char *rfin;
  char *rfout;
  char **args;
} Process;

typedef struct {
  int pgid;
  bool background;
  int nprocesses;
  bool run;
  Process *processes;
} ProcessGroup;

typedef enum {
  PENDING_ARGUMENT,
  ARGUMENT,
  RFIN,
  RFOUT,
  END,
} state;

void printError(){
  write(STDERR_FILENO, ERROR_MSG, strlen(ERROR_MSG));
}

void printProcess(Process *p){

  logPrint("Process %d:\n", p->pid);
  logPrint("\tNumber of Arguments: %d\n", p->nargs);
  logPrint("\tArguments:\n");
  for (int i = 0; i < p->nargs; i++){
    logPrint("\t\t%s\n", p->args[i]);
  }
  logPrint("\tInput File: %s\n", p->rfin);
  logPrint("\tOutput File: %s\n", p->rfout);
}

void printProcessGroup(ProcessGroup *pg){
  logPrint("Process Group %d:\n", pg->pgid);
  logPrint("\tBackground? %s\n", pg->background ? "yes" : "no");
  logPrint("\tNumber of Processes: %d\n", pg->nprocesses);
  logPrint("\tProcesses:\n")
  for (int i = 0; i < pg->nprocesses; i++) {
    printProcess(&(pg->processes[i]));
  }
}

void freeProcess(Process *p) {
  if (p) {
    free(p->rfin);
    p->rfin = NULL;
    free(p->rfout);
    p->rfout = NULL;
    for (int i = 0; i < p->nargs; i++) {
      free(p->args[i]);
      p->args[i] = NULL;
    }
    free(p->args);
    p->args = NULL;
  }
}

void freeGroup(ProcessGroup *pg) {
  if (pg) {
    for (int i = 0; i < pg->nprocesses; i++) {
      freeProcess(&(pg->processes[i]));
    }
    pg->processes = NULL;
  }
}

void cleanup(ProcessGroup *pg, int n) {
  for (int i = 0; i < n; i++) {
    freeGroup(&pg[i]);
  }
  pg = NULL;
}

bool charInString(char a, char *s){
  char b;
  for (int i = 0; (b = s[i]); i++) {
    if (a == b) {
      return true;
    }
  }
  return false;
}

int saveCommandToHistory(char *line) {
  logPrint("Current History (n=%d):\n", N_HISTORY_ENTRIES);
  for (int i = 0; i < N_HISTORY_ENTRIES; i++) {
    logPrint("[%5.i %s]\n", i+1, HISTORY[i]);
  }

  if (
    charInString(line[0], " \t\n") ||
    (N_HISTORY_ENTRIES > 0 && strcmp(line, HISTORY[N_HISTORY_ENTRIES-1]) == 0)
  ) {
    return 0;
  }
  
  int i;
  for (i = 0; i < strlen(line); i++) {
    if (line[i] == '\n'){
      HISTORY[N_HISTORY_ENTRIES][i] = '\0';
      break;
    }
    HISTORY[N_HISTORY_ENTRIES][i] = line[i];
  }
  HISTORY[N_HISTORY_ENTRIES][i] = '\0';

  N_HISTORY_ENTRIES = N_HISTORY_ENTRIES + 1;
  return 0;
}

char* replaceBangs(char **linePtr) {
  char *copy = strdup(*linePtr);
  char *newline = calloc(strlen(copy), sizeof(char) * strlen(copy));
  logPrint("Initial length of newline: %d\n", (int) strlen(newline));
  char *numbers = "1234567890";
  bool replaced = false;

  /*
    Iterate over the line until a ! followed by a number is found
  */
  char *numptr;
  int startidx = 0;
  logPrint("Length of line: %d\n", (int)strlen(copy));
  for (int i = 0; i < strlen(*linePtr); i++) {
    logPrint("Current i: %i, current char: '%c', length of newline: %d\n", i, copy[i], (int) strlen(newline));
    // If char is a newline character, nullify it and end loop
    if (copy[i] == '\n') {
      logPrint("Nullified a \\n\n");
      copy[i] = '\0';
      break;
    }
    else if (copy[i] == '!'){
      if (i == strlen(copy)-1 || !charInString(copy[i+1], numbers)) {
        logPrint("Ignoring bang not followed by a number: %c%c\n", copy[i], copy[i+1]);
        continue;
      }
      /*
        Add NULL first so strcat knows where to end
        Realloc newline to hold itself plus new data from copy
        cat current string onto newline
        (startidx should be the index of the first non-numeric char
        after the latest event replacement )
      */
      copy[i] = '\0';
      int len = strlen(newline) + strlen(copy + startidx) + 1;
      newline = realloc(newline, sizeof(char) * len);
      newline = strcat(newline, copy + startidx);
      /*
        Iterate over the number following the bang and
        copy the number to a new string
        The first digit is 1 past the index i, since i is 
        currently pointed at the !
      */
      char numstr[10];
      numptr = copy + i + 1; // numptr = &copy[i+1]
      int j;
      for (j = 0; charInString(numptr[j], numbers); j++) {
        if (j >= 9) {
          logPrint("Event out of range: !%s... Max digits is 9\n", numstr);
          printError();
          return NULL;
        }
        numstr[j] = numptr[j];
      }

      // Lookup event in history array and cat onto newline if found
      int event = atoi(numstr);
      logPrint("Found event value %d\n", event);
      if (event > 0 && event <= N_HISTORY_ENTRIES) {
        logPrint("Found event: '%s'\n", HISTORY[event-1])
        int len = strlen(newline) + strlen(HISTORY[event-1]) + 1;
        newline = realloc(newline, sizeof(char) * len);
        newline = strcat(newline, HISTORY[event - 1]);
        logPrint("Current line: '%s'\n", newline);
      }
      else {
        logPrint("Event out of range: !%d [1-%d]\n", event, N_HISTORY_ENTRIES);
        printError();
        return NULL;
      }

      replaced = true;
      
      // move startidx and i ahead so on next loop iter 
      // they  will be pointing after the numbers
      i = i + j;
      startidx = i + 1;
      logPrint("Next i: %d, next char: %c[%c]%c\n", i, copy[i-1], copy[i], copy[i+1]);
    }
    logPrint("End of loop, i=%d, length = %d\n", i, (int)strlen(*linePtr));
  }

  // Final copying and resizing
  int len = strlen(newline) + strlen(copy + startidx) + 1;
  newline = realloc(newline, sizeof(char) * len);
  newline = strcat(newline, copy + startidx);

  logPrint("Length of newline: %d\n", (int) strlen(newline));
  // free(*linePtr);
  *linePtr = malloc(sizeof(char) * (strlen(newline) + 1));
  *linePtr = strcpy(*linePtr, newline);
  free(newline);
  free(copy);
  
  if (replaced) {
    printf("%s\n", *linePtr);
  }

  return *linePtr;
}

char* preprocessLine(char **linePtr){
  char *line = *linePtr;
  logPrint("Starting line: '%s'\n", line);

  if (replaceBangs(&line) == NULL) {
    logPrint("replaceBangs failed\n");
    return NULL;
  }
  logPrint("Line after replaceBangs: '%s'\n", line);

  if (saveCommandToHistory(line) < 0) {
    logPrint("saveCommandToHistory failed\n");
    return NULL;
  }

  char *newline = calloc(strlen(line) + 1, sizeof(char));
  int index = 0;

  char prevc = 0;
  char currc = 0;
  char nextc = line[0];
  for (int i = 0; (currc = line[i]); i++) {
    prevc = newline[index-1];
    nextc = line[i+1];

    switch (currc) {
      // Don't need to include case for \n since it was stripped in
      // replaceBangs
      case '\t':
        currc = ' ';
      case ' ':
        // ignore space if it's the beginning/end of the line 
        // or if previous or next character makes it redundant
        if (
          index == 0 || 
          charInString(prevc, "|&<>") || 
          charInString(nextc, " \t\n|&<>\0")
        ){
          continue;
        }
        newline[index++] = currc;
        break;
      case '|':
      case '<':
      case '>':
        if (index == 0) {
          logPrint("Syntax Error: %c found at beginning of line\n", currc);
          printError();
          return NULL;
        }
      case '&':
        if (charInString(prevc, "|&<>")){
          logPrint("Syntax Error: %c found after %c\n", currc, prevc);
          printError();
          free(newline);
          return NULL;
        }
        newline[index++] = currc;
        break;
      default:
        newline[index++] = currc;
    }
  }
  
  if (charInString(newline[strlen(newline)-1], "|<>")) {
    logPrint("Invalid token at end of line: %c\n", newline[strlen(newline)-1]);
    printError();
    free(newline);
    return NULL;
  }
  logPrint("Ending line: %s\n", newline);

  *linePtr = malloc(sizeof(char) * (strlen(newline) + 1));
  if (strcpy(*linePtr, newline) == NULL) {
    logPrint("Error copying stripped line\n");
    exit(1);
  }
  free(newline);
  return *linePtr;
}

void initializeProcess(Process *p, int id){
  p->pid = id;
  p->nargs = 0;
  p->executed = false;
  p->args = NULL;
  p->rfin = NULL;
  p->rfout = NULL;
}

void initializeProcessGroup(ProcessGroup *pg, int id) {
  pg->pgid = id;
  pg->run = false;
  pg->background = true;
  pg->nprocesses = 0;
  pg->processes = NULL;
}

int saveToken(Process *p, char *token, state s) {
  switch (s) {
    case ARGUMENT:
      if ((p->args[p->nargs++] = strdup(token)) == NULL) {
        logPrint("Failed to save token: %s\n", token);
        exit(1);
      }
      break;
    case RFIN:
      if (p->rfin != NULL) {
        logPrint("Trying to save second rfin: %s->%s\n", p->rfin, token);
        printError();
        return -1;
      }
      p->rfin = strdup(token);
      break;
    case RFOUT:
      if (p->rfout != NULL) {
        logPrint("Trying to save second rfout: %s->%s\n", p->rfout, token);
        printError();
        return -1;
      }
      p->rfout = strdup(token);
      break;
    case END:
      logPrint("Attempting to save token during END state: %s\n", token);
      exit(1);
    default:
      logPrint("Encountered unknown state: %i\n", s);
      exit(1);
  }
  return 0;
}

int changeState(state *s, char delim, Process *p) {
  switch(delim) {
    case ' ':
      if (*s == RFIN || *s == RFOUT) {
        logPrint("Error: Trying to add argument after redirect\n");
        printError();
        return -1;
      }
      *s = ARGUMENT;
      break;
    case '<':
      if (p->rfin != NULL) {
        logPrint("Error: Attempting multiple redirect in\n");
        printError();
        return -1;
      }
      else if (p->nargs == 0) {
        logPrint("Error: No command before redirection\n");
        printError();
        return -1;
      }
      *s = RFIN;
      break;
    case '>':
      if (p->rfout != NULL) {
        logPrint("Error: Attempting multiple redirect out\n");
        printError();
        return -1;
      }
      else if (p->nargs == 0) {
        logPrint("Error: No command before redirection\n");
        printError();
        return -1;
      }
      *s = RFOUT;
      break;
    case 0:
      if ((*s == RFIN && p->rfin == NULL) || (*s == RFOUT && p->rfout == NULL)) {
        logPrint("Error: No redirection file specified\n");
        printError();
        return -1;
      }
      *s = END;
      break;
    default:
      logPrint("Encountered unknown deliminater: %c\n", delim);
      exit(1);
  }
  return 0;
}

int parseProcess(Process *p, char *s){
  int maxArgs = 5;
  p->args = malloc(sizeof(*(p->args)) * maxArgs);
  char *cmdToken;
  char *delim = " <>";
  char *copy = strdup(s);

  cmdToken = strtok(s, delim);
  state currState = ARGUMENT;
  while (cmdToken != NULL) {
    if (saveToken(p, cmdToken, currState) != 0){
      logPrint("Save token failed\n");
      return -1;
    }
    char currdelim = copy[cmdToken-s+strlen(cmdToken)];
    logPrint("token=%s, delim=%c\n", cmdToken, currdelim);
    if (changeState(&currState, currdelim, p) != 0){
      return -1;
    }
    cmdToken = strtok(NULL, delim);

    // Resize if all malloc'd arg space has been used up
    if (p->nargs == maxArgs) {
      maxArgs *= 2;
      p->args = realloc(p->args, sizeof(*(p->args)) * maxArgs);
      logPrint("Doubled maxArgs: %d\n", maxArgs);
    }
  }

  // Final resize to number of args 
  // (+ 1 to add ending NULL pointer for execv)
  p->args = realloc(p->args, sizeof(char*) * (p->nargs + 1));
  p->args[p->nargs] = NULL;

  return 0;
}

int parseGroup(ProcessGroup *pg, char *s) {
  int maxProcesses = 1;
  pg->processes = malloc(sizeof(*(pg->processes)) * maxProcesses);
  Process *processPtrs = pg->processes;

  char *processToken;
  char *delim = "|";

  processToken = strsep(&s, delim);

  while (processToken != NULL) {
    // Resize if all of the malloc'd space has been used up
    if (pg->nprocesses == maxProcesses) {
      maxProcesses *= 2;
      pg->processes = realloc(pg->processes, sizeof(*(pg->processes)) * maxProcesses);
      processPtrs = pg->processes;
      logPrint("Doubled number of Processes: %d\n", maxProcesses);
    }

    logPrint("%s\n", processToken);

    initializeProcess(&(processPtrs[pg->nprocesses]), pg->nprocesses);
    if (
      parseProcess(&processPtrs[pg->nprocesses], processToken) != 0) {
      logPrint("parseProcess failed\n");
      return -1;
    }
    pg->nprocesses++;

    processToken = strsep(&s, delim);
  }

  // Final resize
  pg->processes = realloc(pg->processes, sizeof(*(pg->processes)) * pg->nprocesses);

  return 0;
}

int parseLine(ProcessGroup **pgsPtr, char *line){
  // **pgsPtr is a pointer to an array of ProcessGroups

  if (preprocessLine(&line) == NULL) {
    logPrint("preprocessLine failed\n");
    return -1;
  }
  if (strlen(line) == 0){
    return 0;
  }
  logPrint("Line after preprocessesing: %s\n", line);
  char* copy = strdup(line);

  int maxPgs = 1;
  *pgsPtr = malloc(sizeof(**pgsPtr) * maxPgs);
  ProcessGroup *pgs = *pgsPtr;
  int ngroups = 0;

  char *groupToken;
  char *delim = "&";

  groupToken = strsep(&line, delim);

  while (groupToken != NULL) {
    if (ngroups == maxPgs) {
      maxPgs *= 2;
      *pgsPtr = realloc(*pgsPtr, sizeof(**pgsPtr) * maxPgs);
      pgs = *pgsPtr;
      logPrint("Doubled number of ProcessGroups: %d\n", maxPgs);
    }

    logPrint("%s\n", groupToken);
    initializeProcessGroup(&(pgs[ngroups]), ngroups);
    if (parseGroup(&(pgs[ngroups]), groupToken) != 0) {
      logPrint("parseGroup failed\n");
      return -1;
    }
    ngroups++;
    groupToken = strsep(&line, delim);
  }

  // Don't put the last group in the background unless specified
  if (copy[strlen(copy)-1] != '&') {
    logPrint("number of groups: %d\n", ngroups);
    pgs[ngroups-1].background = false;
  }

  // Final resize
  *pgsPtr = realloc(*pgsPtr, sizeof(**pgsPtr) * ngroups);

  return ngroups;
} 

int path(int nargs, char** args) {
  // TODO: might need to check if path given exists?
  if (nargs == 1){
    memset(SEARCH_PATH, 0, sizeof(char) * strlen(SEARCH_PATH));
  }
  else {
    size_t allocSize = MAX_PATH;
    char* buf = (char *)malloc(allocSize);
    char* fullPath = getcwd(buf, allocSize);
    strcat(fullPath, "/");
    for (int i = 1; i < nargs; i++){
      char c = args[i][0];
      strcat(SEARCH_PATH, ";");
      if (c != '/' && c != '~' && !(c=='.'&&args[i][1]=='/')){
        strcat(SEARCH_PATH, fullPath);
      }
      strcat(SEARCH_PATH, args[i]);
    }
    logPrint("New SEARCH_PATH: %s\n", SEARCH_PATH);
    free(buf);
  }
  return 0;
}

int cd(int nargs, char **args) {
  if (nargs != 2) {
    logPrint("Wrong number of args for cd (given %d)\n", nargs);
    printError();
    return -1;
  }
  size_t allocSize = sizeof(char) * 1024;
  char* buf = (char *)malloc(allocSize);
  char* path = getcwd(buf, allocSize);
  strcat(path, "/");
  strcat(path, args[1]);
  if (access(path, F_OK) == 0) {
    chdir(path);
  }
  else {
    logPrint("Path for cd is not accessible: %s\n", path);
    free(buf);
    return -1;
  }
  free(buf);
  return 0;
}

int cat(int nargs, char **args) {
  FILE* f;
  char s[MAX_PATH];

  if (nargs == 1) {
    // Iterate over each line in stdin and print to STDOUT
    while(fgets(s, MAX_PATH, stdin)){
      printf("%s", s);
    }
    return 0;
  }

  // Iterate over each file in command
  // Start at 1 since the first arg is the name of the program
  // If no arguments were given, this loop will not execute and nothing will be printed
  for (int i = 1; i < nargs; i++){

    // open file in read mode
    f = fopen(args[i], "r");

    // If f is NULL, the file does not exist
    if (f == NULL){
      printError();
      return -1;
    }

    // Iterate over each line in the file and print to STDOUT
    while(fgets(s, MAX_PATH, f)){
      printf("%s", s);
    }

    // Close file to prevent memory leaks
    fclose(f);
  }

  return 0;
}

int history(int nargs, char **args) {
  if (nargs != 1) {
    logPrint("Incorrect number of args for history\n");
    printError();
    return -1;
  }

  for (int i = 0; i < N_HISTORY_ENTRIES; i++) {
    printf("%5.i %s\n", i+1, HISTORY[i]);
  }
  return 0;
}

int tryBuiltIn(Process *p) {
  logPrint("Trying builtin for %s\n", p->args[0]);
  char * cmd = p->args[0];

  if (strcmp(cmd, "exit") == 0) {
    if (p->nargs > 1) {
      logPrint("Wrong number of args for exit (given %d, expected 1)\n", p->nargs);
      printError();
      return -1;
    }
    exit(0);
  }
  else if (strcmp(cmd, "path") == 0){
    return path(p->nargs, p->args);
  }
  else if (strcmp(cmd, "showpath") == 0) {
    fprintf(stdout, "%s\n", SEARCH_PATH);
    return 0;
  }
  else if (strcmp(cmd, "cd") == 0) {
    return cd(p->nargs, p->args);
  }
  else if (strcmp(cmd, "cat") == 0) {
    return cat(p->nargs, p->args);
  }
  else if (strcmp(cmd, "history") == 0) {
    return history(p->nargs, p->args);
  }

  return 1;
}

int findOnPath(char *dest, char *tail) {
  char *searchpathcpy = strdup(SEARCH_PATH);
  char *delim = ";";
  char *token;

  token = strtok(searchpathcpy, delim);

  while (token != NULL) {
    dest = strcpy(dest, token);
    dest = strcat(dest, "/");
    dest = strcat(dest, tail);
    if (access(dest, X_OK) == 0) {
      return 0;
    }
    memset(dest, 0, strlen(dest));
    token = strtok(NULL, delim);
  }
  logPrint("Command not found on path: %s\n", tail);
  printError();
  return -1;
}

int redirectIO(Process *p) {
  if (p->rfout != NULL) {
    logPrint("Redirecting output to %s\n", p->rfout);
    int fd = open(p->rfout, O_CREAT|O_WRONLY|O_TRUNC, S_IRWXU);
    if (fd < 0){
      logPrint("Failed to open file for output: %s\n", p->rfout);
      printError();
      return -1;
    }
    else {
      dup2(fd, STDOUT_FILENO);
    } 
  }

  if (p->rfin != NULL) {
    logPrint("Redirecting input to %s\n", p->rfin);
    int fd = open(p->rfin, O_RDONLY, S_IRWXU);
    logPrint("Opened %s on fd %d\n", p->rfin, fd);
    if (fd < 0){
      logPrint("Failed to open file for input: %s\n", p->rfin);
      printError();
      return -1;
    }
    else {
      dup2(fd, STDIN_FILENO);
    } 
  }

  return 0;
}

int executeChild(Process *p) {
  char fullPath[MAX_PATH];
  if (findOnPath(fullPath, p->args[0]) != 0){
    _exit(1);
  }
  // Possible error: not using full path for first arg?
  logPrint("Exec'ing process: %s\n", fullPath);
  execv(fullPath, p->args);
  logPrint("execv failed\n");
  perror("execv");
  _exit(1);
}

int runSingleProcess(Process *p) {
  // Save stdin and stdout
  int savedIn = dup(STDIN_FILENO);
  int savedOut = dup(STDOUT_FILENO);
  logPrint("Saved io on %d and %d\n", savedIn, savedOut);

  if (redirectIO(p) != 0) {
    logPrint("RedirectIO failed\n");
    printError();
    return -1;
  }

  int rc = tryBuiltIn(p);
  if (rc == 1) {
    logPrint("Could not find builtin, executing external command %s\n", p->args[0]);
    rc = fork();
    if (rc < 0) {
      logPrint("Fork failed\n");
      perror("fork");
      exit(1);
    }
    else if (rc == 0) {
      executeChild(p);
    }
    else {
      p->pid = rc;
      logPrint("Waiting for child\n");
      wait(NULL);
    }
  }
  else if (rc < 0) {
    dup2(savedIn, STDIN_FILENO);
    dup2(savedOut, STDOUT_FILENO);
    close(savedIn);
    close(savedOut);
    return -1;
  }

  // Restore stdin and stdout
  logPrint("Restoring stdin/stdout using %d and %d\n", savedIn, savedOut);
  dup2(savedIn, STDIN_FILENO);
  dup2(savedOut, STDOUT_FILENO);
  close(savedIn);
  close(savedOut);

  return 0;
}

void setupPipes(int fdin, int *fdpipe, bool shouldpipeout) {
  logPrint("Duping pipe in\n");
  dup2(fdin, STDIN_FILENO);

  if (shouldpipeout) {
    logPrint("Duping pipe out\n");
    int rc = pipe(fdpipe);
    if (rc < 0){
      perror("pipe");
      exit(1);
    }
    dup2(fdpipe[1], STDOUT_FILENO);
  }
}

int runProcess(Process *p, int fdin, bool shouldpipeout) {
  // Save stdin and stdout
  int savedIn = dup(STDIN_FILENO);
  int savedOut = dup(STDOUT_FILENO);

  // Set up piping in and out
  int fdpipe[2] = {-1, -1};
  setupPipes(fdin, fdpipe, shouldpipeout);

  // Set up redirection (overrides piping if necessary)
  if (redirectIO(p) != 0) {
    logPrint("RedirectIO failed\n");
    return -2;
  }

  // Fork and run
  int rc = fork();
  if (rc < 0) {
    logPrint("Fork failed\n");
    perror("fork");
    exit(1);
  }
  else if (rc == 0) {
    // Close unecessary pipes
    close(fdpipe[0]);

    rc = tryBuiltIn(p);
    if (rc == 1) {
      executeChild(p);
    }
    
    else if (rc == -1) {
      logPrint("tryBuiltinFailed");
      _exit(1);
    }

    _exit(0);
  }
  
  // Save pid to wait on later
  logPrint("Process %d was given pid %d\n", p->pid, rc);
  p->pid = rc;
  p->executed = true;

  /*
    The child now has its own copies of fdin and fdpipe.
    Close the pipe in and pipe out values since we don't
    need them anymore
  */
  close(fdin);
  close(fdpipe[1]);

  // Restore stdin and stdout
  dup2(savedIn, STDIN_FILENO);
  dup2(savedOut, STDOUT_FILENO);
  close(savedIn);
  close(savedOut);

  p->executed = true;
  return fdpipe[0];
}

void runAllGroups(int npgs, ProcessGroup *pgs) {
  // Run all groups without waiting
  logPrint("Running all groups\n");
  for (int i = 0; i < npgs; i++) {
    ProcessGroup *pg = &pgs[i];
    pg->run = true;
    logPrint("Running ProcessGroup %d\n", pg->pgid);

    int pipein = STDIN_FILENO;
    for (int j = 0; j < pg->nprocesses; j++) {
      Process *p = &(pg->processes[j]);
      logPrint("\tRunning Processs %d\n", p->pid);

      bool shouldpipeout = j == pg->nprocesses-1 ? false : true;
      pipein = runProcess(p, pipein, shouldpipeout);
      if (pipein == -2) {
        logPrint("runProcess failed on Process %d (%s)\n", p->pid, p->args[0]);
        break;
      }
      else if (pipein == -1) {
        logPrint("Last Process, pipein value is -1\n");
      }
    }
  }

  // Wait on all processes in all groups
  logPrint("Waiting on all groups\n");
  int status;
  for (int i = 0; i < npgs; i++) {
    ProcessGroup pg = pgs[i];
    logPrint("Waiting on %d Processes in ProcessGroup %d\n", pg.nprocesses, pg.pgid);
    // Don't wait if none of the processes were ever executed
    if (pg.run) {
      for (int j = 0; j < pg.nprocesses; j++) {
        Process p = pg.processes[j];
        printProcess(&p);
        logPrint("\tWaiting on Process %d\n", p.pid);
        // Don't wait if process was never executed
        if (p.executed) {
          waitpid(p.pid, &status, 0);
        }
      }
    }
  } 
}

void run(int npgs, ProcessGroup *pgs){
  // Save stdin and stdout
  int savedIn = dup(STDIN_FILENO);
  int savedOut = dup(STDOUT_FILENO);
  logPrint("Saved io on %d and %d\n", savedIn, savedOut);

  if (npgs == 1 && pgs[0].nprocesses == 1) {
    logPrint("Running a single process\n");
    runSingleProcess(&pgs[0].processes[0]);
  }
  else {
    runAllGroups(npgs, pgs);
  }

  // Restore stdin and stdout
  dup2(savedIn, STDIN_FILENO);
  dup2(savedOut, STDOUT_FILENO);
  close(savedIn);
  close(savedOut);
}

void eval(char *line){
  // Parse line into ProcessGroups and Processes
  // Each ProcessGroup is run in the background together (or not)
  ProcessGroup *pgs; // an array of ProcessGroup structs
  int npgs = parseLine(&pgs, line);

  if (npgs < 0) {
    logPrint("parseLine failed\n");
    return;
  }
  else if (npgs == 0) {
    // If there are no processes, don't run anything
    logPrint("No processes to execute\n");
    return;
  }

  run(npgs, pgs);

  cleanup(pgs, npgs);
  free(pgs);
}

int main(int argc, char** argv){
  char* line;
  size_t size = 0;
  FILE* filein = stdin;
  bool interactive = true;

  if (argc > 2) {
    printError();
    exit(1);
  }
  else if (argc == 2) {
    filein = fopen(argv[1], "r");
    if (filein == NULL) {
      printError();
      exit(1);
    }
    interactive = false;
  }

  while(1){
    if (interactive) {
      printf("wish> ");
    }

    int nchar = getline(&line, &size, filein); 
    if (nchar < 0){
      break;
    }

    eval(line);
  }

  return 0;
}
