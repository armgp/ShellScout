# Terminal-File-Explorer
    File Explorer in terminal for Linux systems. Made using C++.
## Normal Mode
    1. Explorer opens in Normal Mode(Default Mode).
    2. Up and Down arrow keys to navigate through current directory.
    3. Backspace will take you to the parent directory.
    4. Left and Right arrow keys will help navigate through previous visited directories.
    5. H or Home key takes you to the current users home directory.
    6. Enter key enters a directory or opens the file the cursor is pointing to.
    7. Files will be opened in vi.
    8. Press Q/q to exit File Explorer.
    9. Green color names are for executable files.
    10. Blue color names are for directories.
    11. Scrolling features along with vertical and horizontal overflow handling is implemented.

## Command Mode
    1. Press : to enter into Command mode from Normal mode.
    2. In command mode we can use the following commands (given with examples)<br>
      a. $ copy <source_file(s)> <destination_directory> <br>
      b. $ move <source_file(s)> <destination_directory> <br>
      c. $ rename <old_filename> <new_filename><br>
      These commands work for both files and directories<br>
      Any errors will be displayed on status bar.<br>
      d. $ create_file <file_name> <destination_path><br>
      e. $ create_dir <dir_name> <destination_path><br>
      f. $ delete_file <file_path><br>
      g. $ delete_dir <dir_path><br>
      h. $ goto <location><br>
      i. $ search <file_name>, $ search <directory_name> (search returns true/false)<br>
      j. quit command exits from the file explorer<br>
    3. Press Esc to go back to Normal Mode.<br>
  
## Running on your system
    Download main.cpp file and open terminal to the location where main.cpp is stored.<br>
    Now run the following commands<br>
    $ g++ main.cpp<br>
    $ ./a.out<br>
    
## Screenshots
![terminalExplorer](https://user-images.githubusercontent.com/49094298/210721159-4fa7f127-1b27-4f4d-8b3b-ef81e5a16bed.gif)

