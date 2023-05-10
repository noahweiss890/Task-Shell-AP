# Advanced Programming Assignement 1 - myshell

## Authors: 

Rashi Pachino 345174478

Noah Weiss 326876786

## Assumptions

- Our code is written in C

- The name of the .c file is called shell2.c

- The name of the executable is myshell

- When changing the prompt, we assume new prompt is one word only

- If echo $? is the first command, zero will be printed

-  any other arguments on the line after echo $? will not be printed

- If !! is the first command, nothing will be printed

- After Control-C, you must press enter to return to the prompt

- Value of variables created with '$' must be one word

- Additional non-variable arguments sent to echo when printing a variable will be ignored. For example: *echo $Noah* is correct but *echo $Noah says hello* will also just print the value of $Noah

- read command can only create one variable at a time

- To go through history, press up-arrow/down-arrow then enter. To execute the shown command, press enter again

- When at the top of history "no more history UP" will print, when at the bottom of history "no more history DOWN" will print


## To Run
Open terminal at folder and run the following:

*make*

*./myshell*

