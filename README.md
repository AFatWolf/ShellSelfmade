# Shell Handmade
## Introduction
As a homework, I create a inferior shell. 
## Compile & Run
To compile:
```
gcc myshell.c -o myshell
```
To run:
```
./myshell
```
Simple, right? 
## Feature
### Redirect
It supports REDIRECT. The following are the commands type it can execute correctly.
Assume that you already have f1 in your dir and not f2, f3.  
```
cat f1
cat f1 > f2
cat f1 >> f2
cat f1 >&2
cat f3 
cat f1 > f2 > f3
cat p 2> tmp2
```
