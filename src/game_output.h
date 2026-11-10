#ifndef FLXTR_GAME_OUTPUT_H
#define FLXTR_GAME_OUTPUT_H
#ifdef FLXTR_DESKTOP
#define GAME_OUTPUT_DEFAULT 0
#else
#define GAME_OUTPUT_DEFAULT 0
#endif
static int game_output_valid(int height){return height==0||height==240||height==480||height==720||height==1080;}
static int game_output_width(int height){return height==240?320:height==480?640:height==720?1280:height==1080?1920:0;}
#endif
