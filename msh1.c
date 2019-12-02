/* Name  : Zixiu Su
   Net ID: zxs0076
   Mav ID: 1001820076
*/

// The MIT License (MIT)
// 
// Copyright (c) 2016, 2017 Trevor Bakker 
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#if 0 // Debugging Switch
#define _DEBUG_MODE
#endif

#if 0 // Sleep feature Switch (use nano sleep to synchronize output)
#define _ALLOW_SLEEP
#endif

#define _GNU_SOURCE

#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <assert.h>
#include <time.h>

#define WHITESPACE " \t\n"      // We want to split our command line up into tokens
                                // so we need to define what delimits our tokens.
                                // In this case  white space
                                // will separate the tokens on our command line

#define LINEBREAKER ";"           // Input with semicolons will be separated and executed
                                // one at a time.
                                // Each time the input will be cut off at first semicolon,
                                // the remainder will go through next cycle


#define MAX_COMMAND_SIZE 255    // The maximum command-line size

#define MAX_NUM_ARGUMENTS 10    // Mav shell only supports 10 arguments

#define MAX_HISTORY_RECORDS 15  // The maximum size of history records

#define MAX_PID_RECORDS 15      // The maximum size of pid records

#define MAX_PATH_SIZE 512       // Maxmum string size after adding prefix

#define PATH_PREFIX_0 "./"               // Define path priority here.
#define PATH_PREFIX_1 "/usr/local/bin/"  // 
#define PATH_PREFIX_2 "/usr/bin/"        // Child will start from path 0
#define PATH_PREFIX_3 "/bin/"            // and replace by the first executable one

#define CHILD_EXIT_CODE 42      // Exit code if command not found

static pid_t PID = -1;           // A place holder pid value, will be replaced at fork()

static int stat_loc = 0;        // saving exit status for waitpid()

static pid_t bgPID = 0;         // Will save background child's PID (latest)

#ifdef _ALLOW_SLEEP
// Note:
// struct timespec {
//   time_t sec; // second
//   long nsec; // nanosecond
// };

static struct timespec sleep_default = { (time_t)0, (long)80000000 }; // default sleep time 100ms
static struct timespec sleep_rec = { (time_t)0, (long)0 }; // saves sleep record
#endif

// Check if a char pointed by `ptr` appears in a string `set`
int IsElement(char* ptr, const char* set) 
{
  int i = 0;
  while ( i < strlen(set) )
  {
    if ( *ptr == set[i++] ) return 1;
  }
  return 0;
}

// This function trims whitespaces on both ends
// return str pointer passed in
char* TrimWhiteSpace(char* str)
{
  assert(str);
  if ( strlen(str) < 1 ) return str;

  char* temp = strdup(str);
  // end temp string after last non whitespace char
  char* ptr = temp + strlen(str); // point to terminal null char
  while (ptr != temp && IsElement(ptr-1,WHITESPACE))
    --ptr;
  *ptr = 0;
  // move ptr to first non whitespace char
  ptr = temp;
  while ( IsElement(ptr,WHITESPACE) )
    ++ptr;
  // copy
  strcpy(str,ptr);
  free(temp);
  return str;
}

// This function processes user input into well-formatted command tokens (for exec calls)
// Parameters:
// char* str = input to be transformed
// char** token = token array pointer
// int* token_count = saving size of token array
// Return value is for debug info
int Tokenize(char* str, char** token, int* token_count)
{ 

  char *working_str  = strdup( str );
  // we are going to move the working_str pointer so
  // keep track of its original value so we can deallocate
  // the correct amount at the end
  char *working_root = working_str;                                                  
  
  // saveptr for strtok_r
  char *arg_ptr;  

  // Tokenize the input strings with whitespace used as the delimiter
  // Empty tokens will not be saved in the array
  for ( ; *token_count < MAX_NUM_ARGUMENTS; working_str = NULL, ++(*token_count) )
  {
    char* t = strtok_r(working_str, WHITESPACE, &arg_ptr);
    if ( !t )
      break;
    snprintf(token[ *token_count ], MAX_COMMAND_SIZE, "%s", t );
  }
#ifdef _DEBUG_MODE
  printf("\\\\Finishing tokenizing\n");
#endif
  free( working_root );
  return 0;
}
///////////////////////////////////////////////////////////////
// Signal handlers
// (Children replaced by exec image will lose these handlers)
//
// SIGINT (ctrl-c)
void INThandler()
{
#ifdef _DEBUG_MODE
  puts("\\\\INT handler");
#endif
//
// Nothing to do here, image has its own handler, parent has waitpid
//
//   if ( PID && PID != -1 && bgPID) // if Parent and has background Child
//   {
// #ifdef _DEBUG_MODE
//     printf("\\\\Send SIGINT to child pid = %d\n", bgPID);
// #endif
//     if ( kill(bgPID,SIGINT) == 0)
//     {
// #ifdef _DEBUG_MODE
//       puts("\\\\Sig sent.");
// #endif
//       waitpid(bgPID, &stat_loc, WNOHANG); // reap defunct
//       if ( WIFEXITED(stat_loc) )
//       {
//         PID = 0;
//         bgPID = 0;
// #ifdef _DEBUG_MODE
//       puts("\\\\Child exited.");
// #endif        
//       }
//     }
//  }
}

// Suspend child by SIGTSTP (ctrl-z)
// No need to send signal here??? image process will have its own handler
void TSTPhandler()
{
#ifdef _DEBUG_MODE
  puts("\\\\TSTP handler");
#endif
  if ( PID != 0 && PID != -1 )
  {
    waitpid(PID, &stat_loc, WNOHANG);
    if ( WIFSTOPPED(stat_loc) )
      bgPID = PID;
#ifdef _DEBUG_MODE      
    printf("\\\\Child #%d is stopped.\n", bgPID);
#endif
  } 
}
///////////////////////////////////////////////////////////////
int main()
{
  signal(SIGINT, INThandler);
  signal(SIGTSTP, TSTPhandler);

  char* cmd_str = (char*) calloc( MAX_COMMAND_SIZE, sizeof(char) );
  char* working_ptr = cmd_str;

  // If a single line requires multiple cycles of execution,
  // this flag will be set
  int remainder = 0; 
  char* rmd_ptr = NULL;

  // For parsing command tokens
  char* token[MAX_NUM_ARGUMENTS];
  for (int i = 0; i < MAX_NUM_ARGUMENTS; ++i)
    token[i] = (char*)calloc(MAX_COMMAND_SIZE, sizeof(char));

  int token_count = 0;
    
  // concat prefix path + token[0] here
  char* path_str = (char*)calloc(MAX_PATH_SIZE, sizeof(char));

  // this arr will be passed to exec(), easier to change token[0]
  char* token_ptr[MAX_NUM_ARGUMENTS + 1]; // +1 for terminal NULL token

  // History will be saved in this array circularly
  char hist_rec[MAX_HISTORY_RECORDS][MAX_COMMAND_SIZE];
  char* hist_temp = (char*)calloc(MAX_COMMAND_SIZE,sizeof(char));
  int hist_num = 0; // num of history entries
  int hist_head = 0; // keeping track of the oldest index

  // PID will be saved here like history
  int pid_rec[MAX_PID_RECORDS];
  int pid_rec_num = 0; // num of history entries
  int pid_rec_head = 0; // keeping track of the oldest

  while( 1 ) // main loop
  {
    fflush(stdout);
    /* Save entry of the last cycle (circular array) */
    // This needs to be here because the order of the list will shift
    // by a call when it's full
    if ( strlen(hist_temp) ) 
    {
      strncpy(hist_rec[ (hist_head + hist_num) % MAX_HISTORY_RECORDS],
              hist_temp, MAX_COMMAND_SIZE);
      if (hist_num < MAX_HISTORY_RECORDS)
        ++hist_num;
      // Wrap around if full
      else
        hist_head = (hist_head + 1) % MAX_HISTORY_RECORDS;

      memset(hist_temp, 0, MAX_COMMAND_SIZE);
    }

    // Take input from stdin,
    // if remainder is NOT set
    if ( !remainder || !rmd_ptr )
    {

#ifdef _ALLOW_SLEEP
      nanosleep(&sleep_default, &sleep_rec);
#endif
      // Print out the msh prompt
      printf ("msh> ");

      // Read the command from the commandline.  The
      // maximum command that will be read is MAX_COMMAND_SIZE
      // This while command will wait here until the user
      // inputs something since fgets returns NULL when there
      // is no input
      while( !fgets (cmd_str, MAX_COMMAND_SIZE, stdin) );

      /* Trim whitespace at both ends */
      working_ptr = TrimWhiteSpace(cmd_str);
      if ( !working_ptr || !strlen(working_ptr) )
        continue; // empty str, restart loop

#ifdef _DEBUG_MODE      
      printf("\\\\working on string \"%s\"\n", working_ptr);
#endif   
      // saving history to temp
      strcpy( hist_temp, working_ptr );

    }
    else // process remainder string
    {
      /* Trim whitespace at both ends */
      working_ptr = TrimWhiteSpace(rmd_ptr);
      if ( !working_ptr || !strlen(working_ptr) ) 
        continue; // empty str, restart loop
    }

    // clear remainder flag
    remainder = 0;

    // Cut off working string at first semicolon
    char* fsc = strchr( working_ptr, ';'); // if not found strchr returns NULL
    if (fsc)
    {
      remainder = 1;
      rmd_ptr = fsc+1;
      *fsc = 0;
      
#ifdef _DEBUG_MODE    
      printf("\\\\cut = %s, remainder = %s\n", working_ptr, rmd_ptr);
#endif
    }

    if ( !strlen(working_ptr) ) continue;

    // Make sure working_ptr has content
    assert( working_ptr );
    assert( strlen(working_ptr) );
  
    /* Parse input */
    token_count = 0;
    Tokenize(working_ptr, token, &token_count);

    

    for (int i = 0; i < token_count; ++i)
      token_ptr[i] = token[i];
    // Append a NULL token so we can pass them to execvp()
    token_ptr[token_count] = NULL;

#ifdef _DEBUG_MODE      
      printf("\\\\pointing token_ptr arr\n");
#endif
    assert(token_count <= MAX_NUM_ARGUMENTS);
    assert(token_count > 0);

#ifdef _DEBUG_MODE
    // Now print the tokenized input as a debug check
    // TODO Remove this code and replace with your shell functionality
    int token_index  = 0;
    for( token_index = 0; token_index < token_count + 1; token_index ++ ) 
    {
      printf("\\\\token[%d] = %s\n", token_index, token_ptr[token_index] );  
    }
#endif
    assert( token[0] );
    assert( strlen( token[0]) );
    /* Check for built-in commands */
    // These commands require exact format

    // 0. User calling historic entry
    // *** May get stuck in recursive calls
    if (token[0][0] == '!')
    {
      if ( strlen(token[0]) < 2)
      {
        printf("Invalid history call. Format: !%%d\n");
      } 
      int num = atoi(token[0]+1); // atoi returns 0 if cannot convert

#ifdef _DEBUG_MODE      
      printf("\\\\calling history #%d\n", num);
#endif
      // wrong number
      if (num < 1 || num > hist_num)
      {
        printf("Command not in history.\n");
        continue; // restart loop
      }
      // set remainder and restart
      else
      {
        rmd_ptr = hist_rec[ (hist_head+num-1) % MAX_HISTORY_RECORDS];
        remainder = 1;
        continue;
      }
    }

    // 1. Print history
    if ( strcmp("history", token[0]) == 0)
    {
      for (int i = 0; i < hist_num; ++i) {
        printf("%2d\t%s\n", i+1, hist_rec[ (hist_head + i) % MAX_HISTORY_RECORDS]);
      }
      continue; // restart loop after printing history
    }
    // 2. Print PID
    else if ( strcmp("listpids", token[0]) == 0)
    {
      for (int i = 0; i < pid_rec_num; ++i) {
        printf("%2d:\t%d\n", i+1, pid_rec[ (pid_rec_head + i) % MAX_HISTORY_RECORDS]);
      }
      continue; // restart loop after printing pid list
    }
    // 3. exit / quit
    else if ( strcmp("exit", token[0]) == 0 || strcmp("quit", token[0]) == 0 )
    {
      break; // end main loop
    }
    // // 4. fg
    // else if ( strcmp("fg", token[0]) == 0)
    // {
    //   if (bgPID)
    //   {
    //     kill(bgPID, SIGCONT);
    //     stat_loc = 0; //clear stat_loc
    //     waitpid(-1, &stat_loc, WUNTRACED);
    //     // Wake up and decode
    //     if ( WIFEXITED(stat_loc) ) { // Exit
          
    //       if ( stat_loc == 65280)
    //       {
    //         printf("Command not found.\n");
    //       }
    //     }
        
    //     else if ( WIFSTOPPED(stat_loc) ) // Stop
    //     {
    //       bgPID = PID;
    //       // save PID for SIGCONT
    //     }
    //   }
    //   else
    //   {
    //     printf("No child for fg.\n");
    //   }
    //   continue; // restart loop
    // }
#ifdef _DEBUG_MODE
    // debug commands
    else if ( strcmp("printbgPID", token[0]) == 0)
    {
      printf("\\\\bgPID = %d\n", bgPID);
      continue;
    }
    
    else if ( strcmp("printPID", token[0]) == 0)
    {
      printf("\\\\PID = %d\n", PID);
      continue;
    }
#endif

    // 5. bg - run suspended child program and NOT waiting
    else if ( strcmp("bg", token[0]) == 0)
    {
      if (bgPID)
      {
        kill(bgPID, SIGCONT);
        waitpid(-1, &stat_loc, WCONTINUED);
        if ( WIFCONTINUED(stat_loc) )
        {
#ifdef _DEBUG_MODE      
        	printf("\\\\Child continued: pid = %d\n", bgPID );
#endif
          bgPID = 0;
        }
      }
      else
      {
        printf("Cannot find child for bg.\n");
      }
      continue;
    }

    // 6. cd
    // Only the first two tokens will be used
    else if (strcmp("cd", token[0]) == 0)
    {
      assert(token_count > 1);
      int report = chdir(token[1]);
      if (report)
        printf("Invalid path.\n");
      continue; // restart loop;
    }

    /* Fork Child and Exec*/
    PID = fork();
    if (PID == -1)
    {
      perror("Failed to fork: ");
      break;
    }
    
    // Parent process
    if (PID) 
    {
      /* Save PID (circular array) */
      pid_rec[ (pid_rec_head + pid_rec_num) % MAX_PID_RECORDS] = PID;
      if (pid_rec_num < MAX_PID_RECORDS)
        ++pid_rec_num;
      // Wrap around if full
      else
        pid_rec_head = (pid_rec_head + 1) % MAX_PID_RECORDS;

      /* Wait for child */
      stat_loc = 0; //clear stat_loc
      waitpid(PID, &stat_loc, WCONTINUED ^ WUNTRACED);

#ifdef _DEBUG_MODE      
      printf("\\\\parent wakes up: WEXITSTATUS(stat_loc) = %d\n", WEXITSTATUS(stat_loc));
#endif
      /* Wake up and decode status */
      
      if ( WIFEXITED(stat_loc) ) { // Child Exit

        // how to decode status properly???
        if ( WEXITSTATUS(stat_loc) == 42 )
        {
#ifdef _DEBUG_MODE  
          printf("WEXITSTATUS(stat_loc) = %d\n",WEXITSTATUS(stat_loc));
#endif
          printf("Command not found.\n");
        }
        // try to catch background child here
        if (bgPID)
        {
          if( waitpid(bgPID, &stat_loc, WUNTRACED ^ WNOHANG) ) // make sure it catches something
          {
            if ( WIFEXITED(stat_loc) )
            {
  #ifdef _DEBUG_MODE      
        printf("\\\\clearing bgPID #%d, status %d\n", bgPID, WEXITSTATUS(stat_loc) );
  #endif
              bgPID = 0;
            }
          }

        }
      } // end if for Parent
      
      else if ( WIFSTOPPED(stat_loc) ) // Child stopped
      {
#ifdef _DEBUG_MODE      
      printf("\\\\waking by SIGTSTP \n");
#endif
        bgPID = PID; // save bg PID
      }
      continue;
    }
    // Child process
    else
    {
      token_ptr[0] = path_str;
      snprintf(path_str, MAX_PATH_SIZE, "%s%s", PATH_PREFIX_0, token[0]);
#ifdef _DEBUG_MODE      
      printf("\\\\child executing \"%s\"\n", path_str);
#endif
      execvp(token_ptr[0], token_ptr);
      //char* test_argv[] = { "/usr/bin/ls", NULL };
      //execvp(test_argv[0], test_argv);
      
      snprintf(path_str, MAX_PATH_SIZE, "%s%s", PATH_PREFIX_1, token[0]);
#ifdef _DEBUG_MODE
      printf("\\\\child executing \"%s\"\n", path_str);
#endif
      execvp(token_ptr[0], token_ptr);
      
      snprintf(path_str, MAX_PATH_SIZE, "%s%s", PATH_PREFIX_2, token[0]);
#ifdef _DEBUG_MODE      
      printf("\\\\child executing \"%s\"\n", path_str);
#endif
      execvp(token_ptr[0], token_ptr);
      
      snprintf(path_str, MAX_PATH_SIZE, "%s%s", PATH_PREFIX_3, token[0]);
#ifdef _DEBUG_MODE      
      printf("\\\\child executing \"%s\"\n", path_str);
#endif
      execvp(token_ptr[0], token_ptr);

#ifdef _DEBUG_MODE            
      perror("!Child: ");
#endif
      exit(CHILD_EXIT_CODE);
    }
  }// end main while loop

  // free allocated memory
  free(cmd_str);
  free(hist_temp);
  free(path_str);
  for (int i = 0; i < MAX_NUM_ARGUMENTS; ++i)
    free(token[i]);

  return 0;
}
