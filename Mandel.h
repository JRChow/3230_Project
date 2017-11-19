#ifndef MANDEL_H
#define MANDEL_H

#include <math.h>

//Changing the following parameters would affect the computation timing
//Require recompilation
#define MAXITER 50000 			//maximum iteration steps
//Screen dimension constants
#define IMAGE_WIDTH 800
#define IMAGE_HEIGHT 800

//a data type represents the complex number
typedef struct complextype
{
    float real, imag;
} Compl;

//compute each pixel value of the Mandelbrot image
float Mandelbrot(int x, int y){

    int max_iter = MAXITER;
    unsigned int count=0;
    Compl   z, c;
    float   lengthsq, temp;

    z.real = z.imag = 0.0;	//initial value of z

	//the complex plane is from [-2,-2] to [2,2]
	//determine the coordinate of (x,y)
	c.real = -2.0 + 4.0*x/IMAGE_WIDTH;
	c.imag = -2.0 + 4.0*y/IMAGE_HEIGHT;

    do  {    // iterate for pixel value
        temp = z.real*z.real - z.imag*z.imag + c.real;
        z.imag = 2.0*z.real*z.imag + c.imag;
        z.real = temp;
        lengthsq = z.real*z.real+z.imag*z.imag;
        count++;
    } while (lengthsq < 4.0 && count < max_iter);

    //just a heuristic to scale the result for better visualization
    return (1.0 - sqrtf(count)/sqrtf(MAXITER));

}


#endif
