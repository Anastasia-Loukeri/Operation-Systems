#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <readline/readline.h>
#include <readline/history.h>

//Για την μεταγλώττιση:  Απαιτούνται οι εξής ενημερώσεις:
//sudo apt update
//sudo apt install libreadline-dev για το αρχείο κεφαλίδας <readline/readline.h> & <readline/history.h>
//sudo apt install build-essential για το αρχείο κεφαλίδας <sys/wait.h> των POSIX εντολών
//Το αρχείο μεταγλωττίζεται : gcc -Wall tinyshell.c -o tinyshell -lreadline (σύνδεση βιβλιοθηκών)

#define MAX_ARG_CAPACITY 50         // μέγιστος αριθμός ορισμάτων
#define ARG_DELIMITERS " \t\r\n\a" // Νέο set οριοθετών

char *getInputLine() {
    // Δημιουργία δυναμικού prompt με τον τρέχοντα κατάλογο
    char currentDir[1024];
    char promptBuffer[1024 + 30]; // Χώρος για το path + το 'TinyShell> '

    // Παίρνουμε τον τρέχοντα κατάλογο
    if (getcwd(currentDir, sizeof(currentDir)) == NULL) {
        // Αν αποτύχει, χρησιμοποιούμε το default prompt
        strcpy(currentDir, "UnknownPath");
    }

    // Φτιάχνουμε το τελικό prompt: [CurrentDir] TinyShell>
    snprintf(promptBuffer, sizeof(promptBuffer), "[%s] TinyShell> ", currentDir);

    // Η readline() εμφανίζει το prompt και διαχειρίζεται το backspace
    char *line = readline(promptBuffer);
    if (line == NULL) {
        // Χειρισμός EOF (Ctrl+D)
        return NULL;
    }

    if (*line) {
        // Αν η γραμμή δεν είναι κενή, την προσθέτουμε στο ιστορικό
        add_history(line);
    }

    // Η Readline δεν αφήνει trailing '\n'.
    return line;
}

// Διάσπαση της γραμμής εντολής σε tokens
char **splitInputIntoArguments(char *inputLine) {
    int capacity = MAX_ARG_CAPACITY;
    int currentPosition = 0;
    char **arguments = malloc(capacity * sizeof(char*));
    char *currentArg;

    if (!arguments) {
        fprintf(stderr, "TinyShell: Memory allocation failed for arguments\n");
        exit(EXIT_FAILURE);
    }

    // Χρήση της strtok με τους νέους οριοθέτες
    currentArg = strtok(inputLine, ARG_DELIMITERS);

    while (currentArg != NULL) {
        arguments[currentPosition++] = currentArg;

        // Έλεγχος και επαναδέσμευση μνήμης (reallocation) αν χρειαστεί
        if (currentPosition >= capacity) {
            capacity += MAX_ARG_CAPACITY;
            arguments = realloc(arguments, capacity * sizeof(char*));

            if (!arguments) {
                fprintf(stderr, "tinyshell: Reallocation error for arguments\n");
                exit(EXIT_FAILURE);
            }
        }

        currentArg = strtok(NULL, ARG_DELIMITERS);
    }

    // Τοποθέτηση του NULL terminator στο τέλος της λίστας
    arguments[currentPosition] = NULL;
    return arguments;
}

// Eύρεση της πλήρους διαδρομής του εκτελέσιμου
char *resolveCommandPath(char *commandName) {
    // 1. Έλεγχος αν η εντολή είναι πλήρης ή σχετική διαδρομή (περιέχει '/')
    if (strchr(commandName, '/') != NULL) {
        return (access(commandName, X_OK) == 0) ? strdup(commandName) : NULL;
    }

    // 2. Αναζήτηση στις διαδρομές του PATH
    char *pathEnv = getenv("PATH");
    if (!pathEnv) return NULL;

    // Δημιουργία αντιγράφου του PATH για την strtok
    char *pathCopy = strdup(pathEnv);
    char *directory = strtok(pathCopy, ":");
    char fullPathBuffer[1024];
    char *foundPath = NULL; // Αποθηκεύει τη διαδρομή αν βρεθεί

    while (directory != NULL) {
        // Κατασκευή της πλήρους διαδρομής: directory/commandName
        snprintf(fullPathBuffer, sizeof(fullPathBuffer), "%s/%s", directory, commandName);

        if (access(fullPathBuffer, X_OK) == 0) {
            foundPath = strdup(fullPathBuffer);
            break; // Βρέθηκε, τερματίζουμε τον βρόχο
        }

        directory = strtok(NULL, ":");
    }

    free(pathCopy);
    return foundPath; // Επιστροφή της διαδρομής (ή NULL)
}


// Κύριος βρόχος εκτέλεσης του shell
void shellMainLoop() {
    char *inputLine = NULL;
    char **commandArgs = NULL;
    int childStatus = 0;
    pid_t childPID;
    char *execPath = NULL;
    int isRunning = 1; // Μεταβλητή ελέγχου για το command loop

    // Η εξωτερική μεταβλητή __environ περιέχει το περιβάλλον (χρησιμοποιείται στην execve)
    extern char **environ;

    while (isRunning) { // Χρήση while(isRunning)


        inputLine = getInputLine();

        if (!inputLine) { // EOF (Ctrl+D)
            printf("\nTerminating TinyShell...");
            isRunning = 0;
            continue;
        }

        commandArgs = splitInputIntoArguments(inputLine);

        if (!commandArgs[0]) { // Κενή εντολή
            free(inputLine);
            free(commandArgs);
            continue;
        }

        // 1. Χειρισμός ενσωματωμένης εντολής: exit
        if (strcmp(commandArgs[0], "exit") == 0) {
            int exitCode = (commandArgs[1] != NULL) ? atoi(commandArgs[1]) : 0;
            printf("\n");
            free(inputLine);
            free(commandArgs);
            exit(exitCode);
        }

        // **********************************************
        // 2. Χειρισμός ενσωματωμένης εντολής: cd (CHANGE DIRECTORY)
        else if (strcmp(commandArgs[0], "cd") == 0) {
            char *targetDir = commandArgs[1];

            // Αν δεν δοθεί όρισμα (ή δοθεί μόνο ~), πήγαινε στο HOME
            if (targetDir == NULL || strcmp(targetDir, "~") == 0) {
                targetDir = getenv("HOME");
                if (targetDir == NULL) {
                    fprintf(stderr, "TinyShell: cd: HOME environment variable not set. Cannot change to home directory.\n");
                    // Καθαρισμός μνήμης
                    free(inputLine);
                    free(commandArgs);
                    continue;
                }
            }

            // Εκτέλεση της αλλαγής καταλόγου
            if (chdir(targetDir) != 0) {
                perror("TinyShell: cd failed.");
            }

            // Η εντολή cd είναι built-in, οπότε καθαρίζουμε και συνεχίζουμε στον επόμενο βρόχο
            free(inputLine);
            free(commandArgs);
            continue;
        }



        // 3. Αναζήτηση εκτελέσιμου (για εξωτερικές εντολές)
        execPath = resolveCommandPath(commandArgs[0]);
        if (!execPath) {



            // Ελέγχουμε αν η εντολή αναζητήθηκε στο PATH και βρέθηκε κενό/μη ρυθμισμένο.
            if (strchr(commandArgs[0], '/') == NULL && (getenv("PATH") == NULL || getenv("PATH")[0] == '\0')) {
                fprintf(stderr, "TinyShell: Error: Command '%s' not found. PATH environment variable is not set or is empty.\n", commandArgs[0]);
            } else {
                // Γενικό σφάλμα (π.χ. λάθος όνομα εντολής, ή δεν βρέθηκε στο PATH)
                fprintf(stderr, "TinyShell: Error: Command not found: %s .\n", commandArgs[0]);
            }

            childStatus = 127; // Τυπικός κωδικός σφάλματος
            free(inputLine);
            free(commandArgs);
            continue;
        }

        // 4. Εκτέλεση (fork-exec-wait)
        childPID = fork();

        if (childPID == 0) { // Child process
            // Χρηση της execve με το πλήρες path και το environment
            if (execve(execPath, commandArgs, environ) == -1) {
                perror("TinyShell: Command execution failed.");
                // ΣΗΜΑΝΤΙΚΟ: Το παιδικό process πρέπει να καθαρίσει και να βγει
                free(inputLine);
                free(commandArgs);
                free(execPath);
                exit(EXIT_FAILURE);
            }
        } else if (childPID < 0) { // Error
            perror("TinyShell: Forking operation failed");
        } else { // Parent process
            // Αναμονή του child process
            do {
                waitpid(childPID, &childStatus, WUNTRACED);
            } while (!WIFEXITED(childStatus) && !WIFSIGNALED(childStatus));

        if (WIFEXITED(childStatus)) {
            // Εκτυπωση τον κωδικό εξόδου:
            fprintf(stderr, "[Exit Status: %d]\n", WEXITSTATUS(childStatus));
            childStatus = WEXITSTATUS(childStatus);
        } }

        // 5. Καθαρισμός μνήμης (για εξωτερικές εντολές)
        free(inputLine);
        free(commandArgs);
        // Το execPath ελευθερώνεται μόνο εδώ, καθώς δεν χρησιμοποιείται στις built-ins
        free(execPath);
        inputLine = NULL;
        commandArgs = NULL;
        execPath = NULL;
    }
}

int main(void) {
    shellMainLoop();
    return EXIT_SUCCESS;
}
