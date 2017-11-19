#ifndef DRAW_H
#define DRAW_H

#include <SDL2/SDL.h>

//a heuristic to map the value to specific RBG values
void colormap(float val, unsigned char * r, unsigned char * g, unsigned char * b) {
    
    //split the value into 12 groups
    float value = val/0.09;
    //find the base
    unsigned char base = value;

    switch (base) {
        case 0: *r = 0; *g = 0; *b = 127; break;
        case 1: *r = 0; *g = 127; *b = 0; break;
        case 2: *r = 255; *g = 0; *b = 255; break;
        case 3: *r = 0; *g = 255; *b = 255; break;
        case 4: *r = 255; *g = 0; *b = 0; break;
        case 5: *r = 127; *g = 255; *b = 0; break;
        case 6: *r = 188; *g = 143; *b = 143; break;
        case 7: *r = 0; *g = 0; *b = 255; break;
        case 8: *r = 255; *g = 127; *b = 0; break;
        case 9: *r = 255; *g = 127; *b = 127; break;
        case 10: *r = 0; *g = 255; *b = 0; break;
        case 11: *r = 255; *g = 255; *b = 0; break;
    }

}

//This function makes use of the SDL2 library to draw the image
// pixels is the linear array of pixels of the 2D image
// width and height are the dimension of the image
// title is the name to be displayed in the title bar of the window
// delay, in unit of milliseconds, is the duration the window will be displayed
void DrawImage(float* pixels, unsigned int width, unsigned int height, const char * title, unsigned int delay) {

	//The window we'll be rendering to
	SDL_Window* window = NULL;

	//The surface contained by the window
	SDL_Surface* screenSurface = NULL;

	//Initialize SDL
	if( SDL_Init( SDL_INIT_VIDEO ) < 0 )
	{
		printf( "SDL could not initialize! SDL_Error: %s\n", SDL_GetError() );
	}
	else
	{
		//Create window
		window = SDL_CreateWindow(title, 0, 0, width, height, SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE );
		if( window == NULL )
		{
			printf( "Window could not be created! SDL_Error: %s\n", SDL_GetError() );
		}
		else
		{
			//Get window surface
			screenSurface = SDL_GetWindowSurface(window);

			//Create a new surface
			SDL_Surface*  newSurface = SDL_CreateRGBSurface(0, width, height, 32, 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000);
			if (newSurface == NULL) {
        		printf("SDL_CreateRGBSurface() failed: %s", SDL_GetError());
        		exit(1);
    		}

    		//input pixels to the new surface
    		int k;
    		for (k=0; k<width*height; k++) {
    				unsigned char r, g, b;
    				colormap(pixels[k], &r, &g, &b);
    				((unsigned int *)newSurface->pixels)[k] = (0xFF << 24) | (b << 16) | (g << 8) | r;
    		}
			
    		//copy the image to the display window
    		SDL_BlitSurface(newSurface, NULL, screenSurface, NULL);

			//Update the surface
			SDL_UpdateWindowSurface(window);

			//Wait delay milliseconds
			SDL_Delay(delay);
		}
	}

	//Destroy window
	SDL_DestroyWindow(window);

	//Quit SDL subsystems
	SDL_Quit();
	
}


#endif