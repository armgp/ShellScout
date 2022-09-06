// 1. Use of system() library function for command mode functions is not allowed.
// 2. Use of system commands like ls, cp, mv, mkdir etc are not allowed. You have to 
//    implement your own versions of these commands.
// 3. Use of ncurses.h is strictly prohibited. 

/** includes **/
#include <bits/stdc++.h>
#include <unistd.h>
#include <stdlib.h>
#include <termios.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <bits/stdc++.h>
#include <dirent.h>
#include <sys/stat.h>
#include <grp.h>
#include <pwd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <fstream>
#include <iostream>

/**functions**/
void refreshExplorerScreen();
void initWindowSize();
void processKeyPress();
void drawNormalMode();
void drawCommandMode();
void executeCommand();
void executeQuit();
int deleteFile(std::string filePath);
int copyfile(std::string sourceFile, std::string destinationDirectory);
int copyfiles(std::vector<std::string> sourcefiles, std::string destinationDirectory);
std::string getCurrDirectory();
std::string getProcessedDirectoryFilePath(std::string dpath);

/** define **/
#define CTRL_KEY(k) ((k) & 0x1f)
#define LOWTOUP(k) ((k) & 0xdf)

enum editorKey {
  HOME,
  DELETE,
  BACKSPACE=127
};

/** data **/
struct explorerConfig{
    int cx, cy;
    int explorerRows;
    int explorerColumns;
    int expMode=0;
    std::string command="";
    std::string execcommand="";
    struct termios origTermios;
}exCfg;

struct onScreenDirectories{
    std::vector<std::vector<std::string>> directories;
    int currPos=0;
    int startInd=0;
    std::string d=getCurrDirectory();
    std::string commandStatus="";
}osd;

struct currDirectoryDetails{
    std::string user=getpwuid(getuid())->pw_dir;
    std::stack<std::string> bwdHistory;
    std::stack<std::string> fwdHistory;
}currDirDet;


/** append buffer **/
std::string appendBuffer="";

/** terminal **/
void handleWindowSizeChange(){
    int prevCx=exCfg.explorerColumns;
    int prevCy=exCfg.explorerRows;
    initWindowSize();
    int currCx=exCfg.explorerColumns;
    int currCy=exCfg.explorerRows;
    if(currCx!=prevCx || currCy!=prevCy) refreshExplorerScreen();
}

void die(const char* s){
    //screen cleared and cursor repositioned
    //we can also see the error after clearing of the screen
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    write(STDOUT_FILENO, "\x1b[48;2;0;0;0ml", 13);

    perror(s);
    exit(1);
}

void disableRawMode(){
    write(STDOUT_FILENO, "\033[3J", 4);
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &exCfg.origTermios)==-1) die("tcsetattr");

}

void enableRawMode(){
    atexit(disableRawMode);
    if(tcgetattr(STDIN_FILENO, &exCfg.origTermios)==-1) die("tcsetattr");

    struct termios raw = exCfg.origTermios;

    //--flip off ECHO bit to disable printing the key we type on terminal
    //--flip off canonical mode(ICANON) when this bit is made 0, input is taken
    //byte by byte instead of line by line.
    //--(IXTEN flip off) On some systems, when you type Ctrl-V, the terminal waits for you 
    //to type another character and then sends that character literally. 
    //--turning off ISIG will turn off sending SIGINT(Ctrl+C - terminate) and SIGTSTP(Ctrl+Z - suspend)
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    //--Disable Ctrl+S -> stops data from being transmitted to the terminal until you press Ctrl+Q
    //XON comes from the names of the two control characters that Ctrl-S and Ctrl-Q produce:
    // XOFF to pause transmission and XON to resume transmission.
    //--Ctrl-M is weird: it’s being read as 10, when we expect it to be read as 13,
    // since it is the 13th letter of the alphabet, and Ctrl-J already produces a 10.
    //What else produces 10? The Enter key does.
    //It turns out that the terminal is helpfully translating any carriage returns (13, '\r') 
    //inputted by the user into newlines (10, '\n'). Let’s turn off this feature.
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    //We will turn off all output processing features by turning off the OPOST flag. 
    //In practice, the "\n" to "\r\n" translation is likely the only output processing
    //feature turned on by default.
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);

    raw.c_cc[VMIN]=0;
    raw.c_cc[VTIME]=2;

    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw)==-1) die("tcsetattr");
}

char readKey(){
    int n;
    char c;

    //read one byte from the standard input to the variable c
    while((n=read(STDIN_FILENO, &c, 1)) != 1){
        handleWindowSizeChange();
        if(n==-1 && errno != EAGAIN) die("read"); 
    }

    if (c == '\x1b') {
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '1': return HOME;
                        case '3': return DELETE;
                        case '7': return HOME;
                    }
                }
            }
            else{
                switch (seq[1]) {
                    case 'A': return 'w';
                    case 'B': return 's';
                    case 'C': return 'd';
                    case 'D': return 'a';
                    case 'H': return HOME;
                }
            }
        }else if(seq[0]=='O'){
            switch (seq[1]) {
                case 'H': return HOME;
            }
        }

        return '^';
    }

    return c;
}

//reposition cursor to x and y
void repositionCursor(int x, int y, std::string& currBuffer){
    if(x>=exCfg.explorerColumns){
        x=exCfg.explorerColumns;
    }
    if(y>=exCfg.explorerRows){
        y=exCfg.explorerRows;
    }
    std::string X = std::to_string(x+1);
    std::string Y = std::to_string(y+1);
    currBuffer+="\x1b["+Y+";"+X+"H";
}

//move cursor by u,d,f b distances;
void moveCursor(int u, int d, int f, int b, std::string& currBuffer){
    std::string U = std::to_string(u);
    std::string D = std::to_string(d);
    std::string F = std::to_string(f);
    std::string B = std::to_string(b);

    if(u!=0) currBuffer+="\x1b["+U+"A";
    if(d!=0) currBuffer+="\x1b["+D+"B";
    if(f!=0) currBuffer+="\x1b["+F+"C";
    if(b!=0) currBuffer+="\x1b["+B+"D";
}

//getCursorPosition(y, x);
//fallback method if ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) doesnt work in some systems
//to get the width and height of the explorer
int getCursorPosition(int& rows, int& cols){
    const int BUFFER_SIZE =32;
    char buffer[BUFFER_SIZE];
    unsigned int i = 0 ;

    if(write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

    for(int i=0; i<BUFFER_SIZE; i++){
        if(read(STDIN_FILENO, &buffer[i], 1) != 1) break;
        if(buffer[i]=='R'){
            buffer[i]='\0';
            break;
        } 
    }

    if(buffer[0] != '\x1b' || buffer[1] != '[') return -1;
    if (sscanf(&buffer[1], "[%d;%d", &rows, &cols) != 2) return -1;

    return 0;
}

int getWindowSize(int& rows, int& cols){
    struct winsize ws;
    if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws)==-1 || ws.ws_col==0){
        if(write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
        return getCursorPosition(rows, cols);
    }
    else{
        rows=ws.ws_row;
        cols=ws.ws_col;
    }
    return 0;
}

/** init **/
void initExplorer(){
    exCfg.cx=0;
    exCfg.cy=0;
    if(getWindowSize(exCfg.explorerRows, exCfg.explorerColumns)==-1) die("getWindowSize");
}

void initWindowSize(){
    if(getWindowSize(exCfg.explorerRows, exCfg.explorerColumns)==-1) die("getWindowSize");
}

/**util**/
std::string formatBytes(double val){
    double kb = 1024;
    double mb = 1024*1024;
    double gb = 1024*1024*1024;
    double formattedVal;
    std::string res;
    if(val<kb){
        return std::to_string(val);
    } 
    else if(val>kb && val<mb){
        formattedVal = val/kb;
        res+=" KB";
    }else if(val>mb && val<gb){
        formattedVal = val/mb;
        res+=" MB";
    }else{
        formattedVal = val/gb;
        res+=" GB";
    }

    res = std::to_string(formattedVal)+res;
    return res;
}

std::string getPermissions(mode_t permission){
   
    std::string modVal="----------";
    
    modVal[1] = (permission & S_IRUSR) ? 'r' : '-';
    modVal[2] = (permission & S_IWUSR) ? 'w' : '-';
    modVal[3] = (permission & S_IXUSR) ? 'x' : '-';
    modVal[4] = (permission & S_IRGRP) ? 'r' : '-';
    modVal[5] = (permission & S_IWGRP) ? 'w' : '-';
    modVal[6] = (permission & S_IXGRP) ? 'x' : '-';
    modVal[7] = (permission & S_IROTH) ? 'r' : '-';
    modVal[8] = (permission & S_IWOTH) ? 'w' : '-';
    modVal[9] = (permission & S_IXOTH) ? 'x' : '-';
    return modVal;     
}

std::string getCurrDirectory() {
   char buff[FILENAME_MAX]; 
   getcwd(buff, FILENAME_MAX);
   std::string currDir(buff);
   return currDir;
}

int populateCurrDirectory(){
    osd.directories.clear();
    if(osd.d=="") osd.d="/";
    DIR* dir = opendir(osd.d.c_str());
    if(dir==NULL) return 1;
    struct dirent* entity;
    struct stat statBuff;
    entity = readdir(dir);
    
    while (entity!=NULL){
        std::vector<std::string> dirDetails;
        std::string dname(entity->d_name);
        dname=osd.d+"/"+dname;
        stat(dname.c_str(), &statBuff);
        
        
        dirDetails.push_back(entity->d_name);

        double sz = statBuff.st_size;
        std::string fileSize = formatBytes(sz);
        dirDetails.push_back(fileSize);
        if(getgrgid(statBuff.st_uid)) dirDetails.push_back(getgrgid(statBuff.st_uid)->gr_name);
        else dirDetails.push_back("-----");
        if(getpwuid(statBuff.st_gid)) dirDetails.push_back(getpwuid(statBuff.st_gid)->pw_name);
        else dirDetails.push_back("-----");
        
        char time_buf[80];
        // std::string timeBuff;
        struct tm ts;
        ts = *localtime(&statBuff.st_mtim.tv_sec);
        strftime(time_buf, sizeof(time_buf), "%a %Y-%m-%d %H:%M:%S %Z", &ts);
      
        std::string lastMod(asctime(&ts));
        lastMod=lastMod.substr(4,12);
        // std::cout<<lastMod<<"\n";
        // processKeyPress();
        dirDetails.push_back(lastMod);
        std::string permData = getPermissions(statBuff.st_mode);
        dirDetails.push_back(permData);

        if(S_ISDIR(statBuff.st_mode)) {
            dirDetails.back()[0]='d';
            dirDetails.push_back("1");
        }
        else dirDetails.push_back("0");

        osd.directories.push_back(dirDetails);
        
        entity = readdir(dir);
    }
    std::sort(osd.directories.begin(), osd.directories.end());
    //to avoid cursor overflowingthe number of rows printed
    exCfg.cy=0;
    osd.startInd=0;
    osd.currPos=0;
    return 0;
}

void removeLastDir(std::string& d){
    int n = d.size();
    for(int i=n-1; i>=0; i--){
        if(d[i]=='/'){
            d=d.substr(0, i);
            break;   
        }
    }
}

std::string getSourceName(std::string sourcefile){
    int n = sourcefile.size();
    int i=n-1;
    for(; i>=0; i--){
        if(sourcefile[i]=='/') break;
    }
    return sourcefile.substr(i+1);
}

std::string getSourceDirectory(std::string sourcefile){
    int n = sourcefile.size();
    int i=n-1;
    for(; i>=0; i--){
        if(sourcefile[i]=='/') break;
    }
    return sourcefile.substr(0,i+1);
}

/**commands**/
int deleteDirectory(std::string dirPath){
    struct stat statBuff;
    stat(dirPath.c_str(), &statBuff);
    if (!S_ISDIR(statBuff.st_mode)){
        osd.commandStatus="\x1b[91mGiven path is not a directory\x1b[0m";
        return 1;
    }else{
        DIR* dir = opendir(dirPath.c_str());
        if(dir==NULL) {
            osd.commandStatus="\x1b[91mDirectory doesn't exist\x1b[0m";
            return 1;
        }
        struct dirent* entity;
        entity = readdir(dir);
        while (entity!=NULL){
            std::string dname(entity->d_name);
            if(dname!=".." && dname!="."){
                std::string currSource = dirPath+"/"+dname;
                stat(currSource.c_str(), &statBuff);
                if(S_ISDIR(statBuff.st_mode)) {
                    deleteDirectory(currSource);
                }else{
                    deleteFile(currSource);
                }
            }

            entity = readdir(dir);
        }
    }
    rmdir(dirPath.c_str());
    return 0;
}

int deleteFile(std::string filePath){
    std::string processedPath = getProcessedDirectoryFilePath(filePath);
    struct stat statBuff;
    stat(processedPath.c_str(), &statBuff);
    if(S_ISDIR(statBuff.st_mode)){
        osd.commandStatus="\x1b[91mGiven path is a directory. Use delete_dir to delete directories\x1b[0m";
        return 1;
    }
    if(remove(processedPath.c_str())==0) return 0;
    return 1;
}

int deleteFiles(std::vector<std::string> sourcefiles){
    for(std::string sourcefile : sourcefiles){
        if(deleteFile(sourcefile)!=0) return 1;
    }
    return 0;
}

int deleteDirs(std::vector<std::string> sourcedirs){
    for(std::string sourcedir : sourcedirs){
        if(deleteDirectory(getProcessedDirectoryFilePath(sourcedir))!=0) return 1;
    }
    return 0;
}

int copyDirectory(std::string sourceDir, std::string destinationDirectory){
    struct stat statBuff;
    stat(sourceDir.c_str(), &statBuff);
    const int d = mkdir(destinationDirectory.c_str(), statBuff.st_mode);
    if (d==-1){
        osd.commandStatus="\x1b[91mError creating directory!\x1b[0m"+destinationDirectory;
        return 1;
    }

    DIR* dir = opendir(sourceDir.c_str());
    if(dir==NULL) {
        osd.commandStatus="\x1b[91mDirectory doesn't exist\x1b[0m";
        return 1;
    }
    struct dirent* entity;
    entity = readdir(dir);

    while (entity!=NULL){
        std::string dname(entity->d_name);
        if(dname!=".." && dname!="."){
            std::string currSource = sourceDir+"/"+dname;
            stat(currSource.c_str(), &statBuff);
            if(S_ISDIR(statBuff.st_mode)) {
                std::string currDestination = destinationDirectory+"/"+dname;
                if(copyDirectory(currSource, currDestination)==1) return 1;
            }else{
                if(copyfiles({currSource}, destinationDirectory)==1){
                    osd.commandStatus="\x1b[91mcopying interuppted\x1b[0m";
                    return 1;
                }
            }
        }
        
        entity = readdir(dir);
    }

    closedir(dir);
    return 0;
}

int copyfile(std::string sourceFile, std::string destinationDirectory){
    struct stat st;
    
    sourceFile=getProcessedDirectoryFilePath(sourceFile);
    destinationDirectory=getProcessedDirectoryFilePath(destinationDirectory);
    // if(sourceFile==destinationDirectory){
    //     std::string srcName = getSourceName(sourceFile);
    //     int srcNameLen = srcName.size();
    //     std::string newname;
    //     for(int i=srcNameLen-1; i>=0; i--){
    //         if(srcName[i]=='.'){
    //             newname = srcName.substr(0, i)+"(copy)";
    //             newname+=".";
    //             newname+=srcName.substr(i+1);
    //             break;
    //         }
    //         if(i==0){
    //             newname=srcName+"(copy)";
    //         }
    //     }
    //     destinationDirectory=getSourceDirectory(destinationDirectory);
    //     destinationDirectory+=newname;
    // } 
    
    int dstRef;
    int duplicate=0;
    while ((dstRef = open(destinationDirectory.c_str(), O_RDONLY)) != -1){
        duplicate++;
        std::string srcName = getSourceName(sourceFile);
        int srcNameLen = srcName.size();
        std::string newname;
        for(int i=srcNameLen-1; i>=0; i--){
            if(srcName[i]=='.'){
                newname = srcName.substr(0, i)+"(copy"+std::to_string(duplicate)+")";
                newname+=".";
                newname+=srcName.substr(i+1);
                break;
            }
            if(i==0){
                newname=srcName+"(copy"+std::to_string(duplicate)+")";
            }
        }
        destinationDirectory=getSourceDirectory(destinationDirectory);
        destinationDirectory+=newname;
        close(dstRef);
    }

    struct stat statBuff;
    stat(sourceFile.c_str(), &statBuff);
    if(S_ISDIR(statBuff.st_mode)){
        copyDirectory(sourceFile, destinationDirectory);
    }else{
        // std::ifstream  src(sourceFile, std::ios::binary);
        // std::ofstream  dst(destinationDirectory,   std::ios::binary);

        // std::string line;
        // std::ifstream ifile{
        //     sourceFile
        // }; 
        // std::ofstream ofile{ 
        //     destinationDirectory 
        // };
        // if (ifile && ofile) {
        //     while (getline(ifile, line)) {
        //         ofile << line << "\n";
        //     }
        // }
        // else {
        //     return 1;
        // }
        // ifile.close();
        // ofile.close();

        // stat(sourceFile.c_str(), &st);    
        // mode_t perm = st.st_mode;
        // chmod(destinationDirectory.c_str(), perm); 

        int inRef;
        int outRef;
        char *buffer[8192];

        if ((inRef = open(sourceFile.c_str(), O_RDONLY)) == -1){
            osd.commandStatus="\x1b[91mFailed to open sourcefile\x1b[0m";
            return 1;
        }

        if ((outRef = open(destinationDirectory.c_str(), O_CREAT | O_APPEND | O_RDWR, 0666)) == -1){
            osd.commandStatus="\x1b[91mFailed to open newfile\x1b[0m";
            return 1;
        }

        int sz;
        while ((sz=read(inRef, buffer, sizeof(buffer))) > 0)
        {
            write(outRef, buffer, sz);
            bzero(buffer, sizeof(buffer));
        }

        close(outRef);
        close(inRef);

        stat(sourceFile.c_str(), &st);    
        mode_t perm = st.st_mode;
        chmod(destinationDirectory.c_str(), perm); 

    }   
    return 0;
}

int renameObject(std::string sourceFile, std::string newName){
    struct stat statBuff;
    stat(sourceFile.c_str(), &statBuff);
    if(S_ISDIR(statBuff.st_mode)){
        if(copyfile(sourceFile, getSourceDirectory(sourceFile)+newName)!=0) return 1;
        if(deleteDirectory(sourceFile.c_str())!=0) return 1;
    }else{
        if(copyfile(sourceFile, getSourceDirectory(sourceFile)+newName)!=0) return 1;
        if(remove(sourceFile.c_str())!=0) return 1;
    }
    return 0;
}

int copyfiles(std::vector<std::string> sourcefiles, std::string destinationDirectory){
    if(sourcefiles.size()==0){
        osd.commandStatus="\x1b[91mProvide the source file for successful operation\x1b[0m";
        return 1;
    }
    for(std::string sourcefile : sourcefiles){
        std::string sourceName=getSourceName(sourcefile);
        if(sourceName==".." || sourceName=="."){
            osd.commandStatus="\x1b[91mDont use .. or . as source during copying or moving files.\x1b[0m";
            return 1;
        }
        // std::cout<<sourcefile<<" -to- "<<destinationDirectory+"/"+sourceName<<"\n";
        if(copyfile(sourcefile, destinationDirectory+"/"+sourceName)!=0) return 1;
    }
    return 0;
}

int movefiles(std::vector<std::string> sourcefiles, std::string destinationDirectory){
    int status=0;
    if(copyfiles(sourcefiles, destinationDirectory)==1) return 1;
    destinationDirectory=getProcessedDirectoryFilePath(destinationDirectory);
    for(std::string file : sourcefiles){
        file=getProcessedDirectoryFilePath(file);
        std::string srcDir = getSourceDirectory(file);
        srcDir.pop_back();
        if(srcDir==destinationDirectory) return 0;
        struct stat statBuff;
        stat(file.c_str(), &statBuff);
        if(S_ISDIR(statBuff.st_mode)){
            if(deleteDirectory(file)==1) osd.commandStatus="\x1b[91merror in deleting directory\x1b[0m";
        }else{
            if(deleteFile(file)==1) osd.commandStatus="\x1b[91merror in deleting file\x1b[0m";
        }
    }
    return status;
}

int createfile(std::string name, mode_t perms){
    int fd = open(name.c_str(), O_CREAT|O_EXCL, perms);
    if (fd >= 0) {
        return 0;
    } else {
        osd.commandStatus="\x1b[91merror in permissions\x1b[0m";
    }
    return 1;
}

int createfile(std::string name, std::string destinationDirectory){
    mode_t perms = S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH; 
    int fd = open(name.c_str(), O_CREAT|O_EXCL, perms);
    if (fd < 0) {
        osd.commandStatus="\x1b[91mfailed!\x1b[0m";
        return 1;
    } 

    std::string sourceFile=getCurrDirectory()+"/"+name;
    destinationDirectory=osd.d+"/"+destinationDirectory;

    copyfiles({sourceFile}, destinationDirectory);
    remove(sourceFile.c_str());
    osd.commandStatus="\x1b[92mcreated\x1b[0m";
    return 0;
}

int createDirectory(std::string name, std::string destinationDirectory){
    // mode_t perms = S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH; 
    std::string path=destinationDirectory+"/"+name;
    mode_t perms = S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH;
    int d = mkdir(path.c_str(), perms);
    if (d==-1){
        osd.commandStatus="\x1b[91merror creating directory\x1b[0m";
        return 1;
    }
    osd.commandStatus="\x1b[92mcreated folder\x1b[0m";
    return 0;
}

std::vector<std::string> getComponentsOfDir(std::string destinationDirectory){
    std::vector<std::string> destinationComponents;
    int n = destinationDirectory.size();
    int start=0;
    for(int i=0; i<=n; i++){
        if(i==n || destinationDirectory[i]=='/'){
            destinationComponents.push_back(destinationDirectory.substr(start, i-start));
            start=i+1;
        }
    }
    return destinationComponents;
}

std::string getProcessedDirectoryFilePath(std::string destinationDirectory){
    std::string currdir = osd.d;
    std::vector<std::string> destinationComponents = getComponentsOfDir(destinationDirectory);
    if(destinationComponents.size()>1 && destinationComponents[1]=="home"){
        currDirDet.bwdHistory.push(osd.d);
        return destinationDirectory;
    }else if(destinationComponents[0]=="home"){
        currDirDet.bwdHistory.push(osd.d);
        return "/"+destinationDirectory;
    }
    std::vector<std::string> currDirComponents = getComponentsOfDir(currdir);
    for(std::string comp : destinationComponents){
        if(comp=="..") {
            if(currDirComponents.size()>0) currDirComponents.pop_back();
        }else if(comp=="."){
            continue;
        }else currDirComponents.push_back(comp);
    }
    currdir="";
    for(std::string comp : currDirComponents){
        currdir+=comp+"/";
    }
    currdir.pop_back();
    return currdir;
}

void gotoFunction(std::string destinationDirectory){
    int d;
    if((d = open(getProcessedDirectoryFilePath(destinationDirectory).c_str(), O_RDONLY))==-1){
        osd.commandStatus="\x1b[91mDirectory doesnt exist\x1b[0m";
        close(d);
        return;
    }

    currDirDet.bwdHistory.push(osd.d);
    if(getProcessedDirectoryFilePath(destinationDirectory)=="failed") return;
    else osd.d=getProcessedDirectoryFilePath(destinationDirectory);
}

bool searchForObject(std::string objName, std::string sourceDir){
    struct stat statBuff;

    DIR* dir = opendir(sourceDir.c_str());
    if(dir==NULL) return false;
    struct dirent* entity;
    entity = readdir(dir);

    while (entity!=NULL){
        std::string dname(entity->d_name);
        if(dname!=".." && dname!="."){
            if(dname==objName) {
                closedir(dir);
                return true;
            }
            std::string currSource = sourceDir+"/"+dname;
            stat(currSource.c_str(), &statBuff);
            if(S_ISDIR(statBuff.st_mode)) {
                if(searchForObject(objName, currSource)){
                    closedir(dir);
                    return true;
                } 
            }
        }
        entity = readdir(dir);
    }

    closedir(dir);
    return false;;
}

/** input **/
void processKeyPress(){
    
    char c = readKey();
    //IF CTRL+q exit
    //here since the 5th bit is also discarded 
    //it doesnt matter if its lowercase/uppercase q.
    //5th bit set to 1 is lowercase
    if(c=='w' && exCfg.cy>=1 && exCfg.expMode==0){
        if(exCfg.cy==1 && osd.startInd>0){
            if(osd.currPos>0){
                osd.startInd--;
                osd.currPos--;
            }
        }
        else {
            exCfg.cy--;
            osd.currPos--;
        }
    }
    else if(c=='s' && exCfg.cy<osd.directories.size()-1 && exCfg.cy<=exCfg.explorerRows-4 && exCfg.expMode==0){
        if(exCfg.cy==exCfg.explorerRows-4){
            if(osd.currPos < osd.directories.size()-1){
                osd.startInd++;
                osd.currPos++;
            }
        }
        else {
            exCfg.cy++;
            osd.currPos++;
        }
    }
    else if(c=='d' && exCfg.expMode==0){
        if(!currDirDet.fwdHistory.empty()){
            currDirDet.bwdHistory.push(osd.d);
            osd.d=currDirDet.fwdHistory.top();
            currDirDet.fwdHistory.pop();
            populateCurrDirectory();
        }
    }
    else if(c=='a' && exCfg.expMode==0){
        if(!currDirDet.bwdHistory.empty()){
            currDirDet.fwdHistory.push(osd.d);
            osd.d=currDirDet.bwdHistory.top();
            currDirDet.bwdHistory.pop();
            populateCurrDirectory();
        }
    }
    else if(c==DELETE && exCfg.expMode==0){
        //to change the colors back to default 
        write(STDOUT_FILENO, "\x1b[0m", 4);
        //to clear screen when exiting
        write(STDOUT_FILENO, "\x1b[2J", 4);
        exit(0);
    }
    else if((c==HOME || c== 'h' || c== 'H') && exCfg.expMode==0){
        currDirDet.bwdHistory.push(osd.d);
        osd.d=currDirDet.user;
        populateCurrDirectory();
    }
    else if(c==BACKSPACE && exCfg.expMode==0){
        currDirDet.bwdHistory.push(osd.d);
        removeLastDir(osd.d);
        populateCurrDirectory();
    }
    else if(c=='\r' && exCfg.expMode==0){
        if(osd.directories[osd.currPos][6]=="1" && osd.directories[osd.currPos][0]!="."){
            currDirDet.bwdHistory.push(osd.d);
            if(osd.directories[osd.currPos][0]=="..") removeLastDir(osd.d);
            else osd.d+="/"+osd.directories[osd.currPos][0];
            populateCurrDirectory();
        }else if(osd.directories[osd.currPos][6]=="0"){
            std::string temp=osd.d;
            osd.d+="/"+osd.directories[osd.currPos][0];
            //only child process will have fork()=0
            int pid=fork();
            if(pid==0){
                execl("/usr/bin/vi", "vi", osd.d.c_str(), (char *)0);
                exit(1);
            }else{
                int wval, status=0;
                while ((wval=waitpid(pid, &status, 0)) != pid){
                    if (wval == -1) exit(1); 
                }
            }
            osd.d=temp;
        }
    }
    else if(c==':' && exCfg.expMode==0){
        exCfg.expMode=1;
    }else if(c=='\x1b' && exCfg.expMode==1){
        exCfg.expMode=0;
    }
    else if((c=='Q' || c=='q') && exCfg.expMode==0){
        executeQuit();
    }else if(exCfg.expMode==1){
        if(c==BACKSPACE){
            int y,x;
            getCursorPosition(y,x);
            if((y==exCfg.explorerRows-1 && x>18) || y>exCfg.explorerRows-1){
                if(y>exCfg.explorerRows-1 && x==1){
                    moveCursor(1,0,exCfg.explorerColumns,0, exCfg.command);
                }else{
                    moveCursor(0,0,0,1, exCfg.command);
                    exCfg.command+="\x1b[K";
                    exCfg.execcommand.pop_back();
                }
            }
        } 
        else if(c=='\r'){
            executeCommand();
        }else if(c=='\t'){
            exCfg.command+=c;
            exCfg.execcommand+="        ";
        }
        else {
            exCfg.command+=c;
            exCfg.execcommand+=c;
        }
    }
  
}

void executeQuit(){
    //to change the colors back to default 
    write(STDOUT_FILENO, "\x1b[0m", 4);

    write(STDOUT_FILENO, "\x1b[H", 3);
    //to clear screen when exiting
    write(STDOUT_FILENO, "\033[2J", 4);
    write(STDOUT_FILENO, "\033[3J", 4);
    exit(0);
}

 std::vector<std::vector<std::string>> processCommand(std::string command){
    std::vector<std::vector<std::string>> allCommands;
    int n = command.size();
    std::vector<std::string> components;
    int start=0;
    for(int i=0; i<n; i++){
        if(command[start]==' ') return {{"\x1b[91minvalid command\x1b[0m"}};
        if(command[i]=='~' && i+1<n-1 && command[i+1]=='/'){
            start=i+2;
            i+=2;
        }
        if(command[i]==' ' || i==n-1){
            int count;
            if(i==n-1) count = n-start;
            else count = i-start;
            components.push_back(command.substr(start, count));
            start=i+1;
        }
    }

    int m = components.size();
    std::vector<std::string> comm = {components[0]};
    for(int i=1; i<m; i++){
        if(components[i]=="copy" || components[i]=="move" || components[i]=="rename" || 
        components[i]=="create_file" || components[i]=="create_dir" || components[i]=="delete_file" || 
        components[i]=="delete_dir" || components[i]=="goto" || components[i]=="search" || components[i]=="quit"){
            allCommands.push_back(comm);
            comm={components[i]};
        }else{
            comm.push_back(components[i]);
            if(components[i]=="quit") return allCommands;
        }
    }
    allCommands.push_back(comm);
    return allCommands;
}

void executeCommand(){
    std::string command = exCfg.execcommand;

    exCfg.command="";
    exCfg.execcommand="";
    if(command=="") return;
    std::vector<std::vector<std::string>> allCommands = processCommand(command);

    for(std::vector<std::string> components : allCommands){
        if(components[0]=="quit") executeQuit();
        else {
            int n=components.size();
            if(components[0]=="copy" || components[0]=="move"){

                std::vector<std::string> sourcefiles;
                std::string destinationDirectory;

                for(int i=1; i<n-1; i++){
                    std::string sourceFile=components[i];
                    sourcefiles.push_back(sourceFile);
                }

                destinationDirectory=components[n-1];
                // destinationDirectory=osd.d+"/"+destinationDirectory;

                if(components[0]=="copy" ){
                    if(copyfiles(sourcefiles, destinationDirectory)==0) osd.commandStatus="\x1b[92mexecuted!\x1b[0m";
                }else if(components[0]=="move" ){
                    if(movefiles(sourcefiles, destinationDirectory)==0) osd.commandStatus="\x1b[92mexecuted!\x1b[0m";
                }
            }else if(components[0]=="rename"){
                std::string sourceFile=osd.d+"/"+components[1];
                std::string newName;
                for(int i=2; i<n; i++){
                    newName+=components[i];
                    if(i!=n-1)newName+=" ";
                }
                if(renameObject(sourceFile, newName)!=0) osd.commandStatus="\x1b[91mfailed!\x1b[0m";
                else osd.commandStatus="\x1b[92mexecuted!\x1b[0m";
            }else if(components[0]=="create_file"){
                if(components.size()>=3) createfile(components[1], components[2]);
                else osd.commandStatus="\x1b[91minvalid command\x1b[0m";
            }else if(components[0]=="delete_file"){
                if(components.size()>=2) {
                    std::vector<std::string> sourcefiles;
                    int n=components.size();
                    for(int i=1; i<n; i++){
                        std::string sourceFile=components[i];
                        sourcefiles.push_back(sourceFile);
                    }
                    if(deleteFiles(sourcefiles)==0) osd.commandStatus="\x1b[92mdeleted\x1b[0m";
                    else osd.commandStatus="\x1b[91merror in deleting\x1b[0m";
                }
                else osd.commandStatus="\x1b[91minvalid command\x1b[0m";
            }else if(components[0]=="create_dir"){
                if(components.size()>=3) createDirectory(components[1], getProcessedDirectoryFilePath(components[2]));
                else osd.commandStatus="invalid command";
            }else if(components[0]=="delete_dir"){
                if(components.size()>=2){
                    std::vector<std::string> sourcedirs;
                    int n=components.size();
                    for(int i=1; i<n; i++){
                        std::string sourceDir=components[i];
                        sourcedirs.push_back(sourceDir);
                    }
                    if(deleteDirs(sourcedirs)==0) osd.commandStatus="\x1b[92mdeleted\x1b[0m";
                    else osd.commandStatus="error in deleting";
                } 
                else osd.commandStatus="invalid command";
            }else if(components[0]=="goto"){
                std::string dir="";
                for(int i=1; i<n; i++){
                    dir+=components[i]+" ";
                }
                dir.pop_back();
                gotoFunction(dir);
            }else if(components[0]=="search"){
                if(components.size()>=2) {
                    if(searchForObject(components[1], osd.d)) osd.commandStatus="\x1b[92mTRUE\x1b[0m";
                    else osd.commandStatus="\x1b[91mFALSE\x1b[0m";
                }
                else osd.commandStatus="invalid command";
            }
            else{
                osd.commandStatus="\x1b[91mInvalid Command!\x1b[0m";
            }
        }
    }

    populateCurrDirectory();
}

/** output **/
void drawNormalMode(){
    int n = exCfg.explorerRows;
    int m = osd.directories.size();
    int eCols=exCfg.explorerColumns;
    int dCols=7;
    int maxOccupancy = eCols/dCols;
    // std::vector<std::string> columnHeadings = {"FILE_NAME", "FNAME", "FILE_SIZE", "FSIZE", "USER", "U", "GROUP", "G", "LAST_MOD", "LMOD", "PERMISSIONS", "PERM"};
    std::string draw;
    for(int i=0; i<=n; i++){
        if(i==n-1){
            draw+="\x1b[K\x1b[H";
            break;
        }else if(i==0){
            draw+="\x1b[K\x1b[1G";
            moveCursor(0,0,5,0,draw);
            draw+="\033[34;4;3mF_NAME";

            draw+="\x1b[1G";
            moveCursor(0,0,maxOccupancy+5,0,draw);
            draw+="\033[34;4;3mF_SIZE";

            draw+="\x1b[1G";
            moveCursor(0,0,2*maxOccupancy+5,0,draw);
            draw+="\033[34;4;3mUSR";

            draw+="\x1b[1G";
            moveCursor(0,0,3*maxOccupancy+5,0,draw);
            draw+="\033[34;4;3mGRP";

            draw+="\x1b[1G";
            moveCursor(0,0,4*maxOccupancy+5,0,draw);
            draw+="\033[34;4;3mLST_MOD";

            draw+="\x1b[1G";
            moveCursor(0,0,5*maxOccupancy+5,0,draw);
            draw+="\033[34;4;3mPERM\033[0m";
        }else if(i==n-2) {
            draw+="\x1b[K\033[40;1;7m Normal Mode: ";
            draw+=osd.d.substr(0, exCfg.explorerColumns-20);
            if(exCfg.explorerColumns-20<osd.d.size()) draw+="..";
            draw+="\033[0m";
        }else draw+="\x1b[K";

        
        if(i>0 && (i-1+osd.startInd)<m && i<n-2){
            std::vector<std::string> dirDetails = osd.directories[i-1+osd.startInd];
            
            moveCursor(0,0,5,0,draw);
            if(dirDetails[5][0]=='d') draw+="\033[34;3;1m";
            else if(dirDetails[5][3]=='x') draw+="\033[32;3;1m";
            if(dirDetails[0].size()<maxOccupancy) draw+=dirDetails[0];
            else draw+=dirDetails[0].substr(0, maxOccupancy-3)+"..";
            if(dirDetails[5][0]=='d' || dirDetails[5][3]=='x') draw+="\033[0m";

            draw+="\x1b[1G";
            moveCursor(0,0,maxOccupancy+5,0,draw);
            if(dirDetails[1].size()<maxOccupancy) draw+=dirDetails[1];
            else draw+=dirDetails[1].substr(0, maxOccupancy-3)+"..";

            draw+="\x1b[1G";
            moveCursor(0,0,2*maxOccupancy+5,0,draw);
            if(dirDetails[2].size()<maxOccupancy) draw+=dirDetails[2];
            else draw+=dirDetails[2].substr(0, maxOccupancy-3)+"..";

            draw+="\x1b[1G";
            moveCursor(0,0,3*maxOccupancy+5,0,draw);
            if(dirDetails[3].size()<maxOccupancy) draw+=dirDetails[3];
            else draw+=dirDetails[3].substr(0, maxOccupancy-3)+"..";

            draw+="\x1b[1G";
            moveCursor(0,0,4*maxOccupancy+5,0,draw);
            if(dirDetails[4].size()<maxOccupancy) draw+=dirDetails[4];
            else draw+=dirDetails[4].substr(0, maxOccupancy-3)+"..";

            draw+="\x1b[1G";
            moveCursor(0,0,5*maxOccupancy+5,0,draw);
            if(dirDetails[5].size()<maxOccupancy) draw+=dirDetails[5];
            else draw+=dirDetails[5].substr(0, maxOccupancy-3)+"..";
        }

        draw+="\r\n";
    }
    appendBuffer+=draw;
    //+1 because indexing starts from 1 in the screen
    std::string cy=std::to_string(exCfg.cy+1);
    std::string cx=std::to_string(exCfg.cx+1);

    // appendBuffer+=cx

    // add "\x1b[cy;cxH" to appendBuffer
    std::string positionCursor ="\x1b[";
    positionCursor+=cy;
    positionCursor+=";";
    positionCursor+=cx;
    positionCursor+="H\r\n";
    appendBuffer+=positionCursor;

    //show cursor after drawing
    appendBuffer+="\x1b[?25h";
    // repositionCursor(40, 20, appendBuffer);
    write(STDOUT_FILENO, appendBuffer.c_str(), appendBuffer.size());
    appendBuffer="";

    int x, y;
    getCursorPosition(y, x);
    int index = y-2;
    appendBuffer+="\x1b[0;0;60b";
}

void drawCommandMode(){
    //NORMAL SCREEN
    int n = exCfg.explorerRows;
    int m = osd.directories.size();
    int eCols=exCfg.explorerColumns;
    int dCols=7;
    int maxOccupancy = eCols/dCols;
    // std::vector<std::string> columnHeadings = {"FILE_NAME", "FNAME", "FILE_SIZE", "FSIZE", "USER", "U", "GROUP", "G", "LAST_MOD", "LMOD", "PERMISSIONS", "PERM"};
    std::string draw;
    for(int i=0; i<=n; i++){
        if(i==n-1){
            draw+="\033[3J\x1b[K\x1b[H";
            break;
        }else if(i==0){
            draw+="\x1b[K\x1b[1G";
            moveCursor(0,0,5,0,draw);
            draw+="\033[34;4;3mF_NAME";

            draw+="\x1b[1G";
            moveCursor(0,0,maxOccupancy+5,0,draw);
            draw+="\033[34;4;3mF_SIZE";

            draw+="\x1b[1G";
            moveCursor(0,0,2*maxOccupancy+5,0,draw);
            draw+="\033[34;4;3mUSR";

            draw+="\x1b[1G";
            moveCursor(0,0,3*maxOccupancy+5,0,draw);
            draw+="\033[34;4;3mGRP";

            draw+="\x1b[1G";
            moveCursor(0,0,4*maxOccupancy+5,0,draw);
            draw+="\033[34;4;3mLST_MOD";

            draw+="\x1b[1G";
            moveCursor(0,0,5*maxOccupancy+5,0,draw);
            draw+="\033[34;4;3mPERM\033[0m";
        }else if(i==n-2) {
        }else draw+="\x1b[K";

        
        if(i>0 && (i-1+osd.startInd)<m && i<n-2){
            std::vector<std::string> dirDetails = osd.directories[i-1+osd.startInd];
            
            moveCursor(0,0,5,0,draw);
            if(dirDetails[5][0]=='d') draw+="\033[34;3;1m";
            else if(dirDetails[5][3]=='x') draw+="\033[32;3;1m";
            if(dirDetails[0].size()<maxOccupancy) draw+=dirDetails[0];
            else draw+=dirDetails[0].substr(0, maxOccupancy-3)+"..";
            if(dirDetails[5][0]=='d' || dirDetails[5][3]=='x') draw+="\033[0m";

            draw+="\x1b[1G";
            moveCursor(0,0,maxOccupancy+5,0,draw);
            if(dirDetails[1].size()<maxOccupancy) draw+=dirDetails[1];
            else draw+=dirDetails[1].substr(0, maxOccupancy-3)+"..";

            draw+="\x1b[1G";
            moveCursor(0,0,2*maxOccupancy+5,0,draw);
            if(dirDetails[2].size()<maxOccupancy) draw+=dirDetails[2];
            else draw+=dirDetails[2].substr(0, maxOccupancy-3)+"..";

            draw+="\x1b[1G";
            moveCursor(0,0,3*maxOccupancy+5,0,draw);
            if(dirDetails[3].size()<maxOccupancy) draw+=dirDetails[3];
            else draw+=dirDetails[3].substr(0, maxOccupancy-3)+"..";

            draw+="\x1b[1G";
            moveCursor(0,0,4*maxOccupancy+5,0,draw);
            if(dirDetails[4].size()<maxOccupancy) draw+=dirDetails[4];
            else draw+=dirDetails[4].substr(0, maxOccupancy-3)+"..";

            draw+="\x1b[1G";
            moveCursor(0,0,5*maxOccupancy+5,0,draw);
            if(dirDetails[5].size()<maxOccupancy) draw+=dirDetails[5];
            else draw+=dirDetails[5].substr(0, maxOccupancy-3)+"..";
        }

        draw+="\r\n";
    }
    appendBuffer+=draw;
    //+1 because indexing starts from 1 in the screen
    std::string cy=std::to_string(exCfg.cy+1);
    std::string cx=std::to_string(exCfg.cx+1);

    // appendBuffer+=cx

    // add "\x1b[cy;cxH" to appendBuffer
    std::string positionCursor ="\x1b[";
    positionCursor+=cy;
    positionCursor+=";";
    positionCursor+=cx;
    positionCursor+="H\r\n";
    appendBuffer+=positionCursor;

    //show cursor after drawing
    appendBuffer+="\x1b[?25h";

    int x, y;
    getCursorPosition(y, x);
    int index = y-2;
    appendBuffer+="\x1b[0;0;60b";


    //COMMAND TAB
    repositionCursor(1, exCfg.explorerRows-1, draw);
    draw+=osd.commandStatus;
    repositionCursor(1, exCfg.explorerRows-2, draw);
    draw+="\x1b[K\033[40;1;7m Command Mode:\033[0m$ ";
    draw+=exCfg.command;
    
    osd.commandStatus="";
    appendBuffer+=draw;
}

void drawExplorerRows(){
    if(exCfg.expMode==0){
        drawNormalMode();
    }else{
        drawCommandMode();
    }
}

void refreshExplorerScreen(){
    //hide cursor while drawing
    appendBuffer+="\x1b[?25l";

    /* DELETED-(We dont want to clear the entire screen each time we refresh)
    // \x1b is the escape charachter(byte)=27 which is always in front of escape sequences
    // escape seq always starts with esc char and [
    // after [ comes the argument which is 2(clear the entire screen) and command which is J(Erase In Display)
    //4bytes
    appendBuffer+="\x1b[2J";
    */

    //reposition cursor. The default is \x1b[1;1H == \x1b[H, so only need to write 3 bytes
    //row and column starts from 1 and not 0.
    appendBuffer+="\x1b[3J\x1b[H";
    
    // write(STDOUT_FILENO, "\x1b[H", 3);

    drawExplorerRows();
    
    write(STDOUT_FILENO, appendBuffer.c_str(), appendBuffer.size());
    appendBuffer="";
    // std::cout<<x<<" "<<y<<"\n";
}

int main(){

    if(populateCurrDirectory()!=0) die("populateCurrDirectory");
    enableRawMode();
    initExplorer();

    while (1){
        refreshExplorerScreen();
        processKeyPress();
    }

    return 0;
}