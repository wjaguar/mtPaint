/*
	The following quantizing algorithm is the work of Xiaolin Wu - see the attached notes

	I downloaded it from:
	http://www.ece.mcmaster.ca/~xwu/cq.c

	During September 2005 I adjusted the code slightly to get it to work with mtPaint,
	and for the code to conform to my programming style,
	but the colour selection algorithm remains the same.

	Mark Tyler, September 2005.

	I updated the integration code to use mtPaint 3.30 interfaces.
	Dmitry Groshev, July 2008.
	And added "diameter weighting" mode.
	Dmitry Groshev, November 2008.
	Switched from float to double for better numeric stability.
	Dmitry Groshev, November 2013.
*/

#include "mygtk.h"
#include "memory.h"

/*
Having received many constructive comments and bug reports about my previous
C implementation of my color quantizer (Graphics Gems vol. II, p. 126-133),
I am posting the following second version of my program (hopefully 100%
healthy) as a reply to all those who are interested in the problem.
*/

/**********************************************************************
	    C Implementation of Wu's Color Quantizer (v. 2)
	    (see Graphics Gems vol. II, pp. 126-133)

Author:	Xiaolin Wu
	Dept. of Computer Science
	Univ. of Western Ontario
	London, Ontario N6A 5B7
	wu@csd.uwo.ca

Algorithm: Greedy orthogonal bipartition of RGB space for variance
	   minimization aided by inclusion-exclusion tricks.
	   For speed no nearest neighbor search is done. Slightly
	   better performance can be expected by more sophisticated
	   but more expensive versions.

The author thanks Tom Lane at Tom_Lane@G.GP.CS.CMU.EDU for much of
additional documentation and a cure to a previous bug.

Free to distribute, comments and suggestions are appreciated.
**********************************************************************/	

#define MAXCOLOR	256
#define	RED	2
#define	GREEN	1
#define BLUE	0

struct box { int r0, r1, g0, g1, b0, b1, vol;	// min value, exclusive. max value, inclusive
		};

/* Histogram is in elements 1..HISTSIZE along each axis,
 * element 0 is for base or marginal value
 * NB: these must start out 0!
 */

static double	*m2;
static int	*wt, *mr, *mg, *mb;

static int	size; // image size
static int	K;    // color look-up table size

static void Hist3d(inbuf, vwt, vmr, vmg, vmb) 	// build 3-D color histogram of counts, r/g/b, c^2
unsigned char *inbuf;
int *vwt, *vmr, *vmg, *vmb;
{
	register int ind, r, g, b;
	int	     inr, ing, inb, table[256];
	register long int i;
		
	for(i=0; i<256; ++i) table[i]=i*i;

	for(i=0; i<size; ++i)
	{
		r = inbuf[0];
		g = inbuf[1];
		b = inbuf[2];
		inbuf += 3;
		inr=(r>>3)+1; 
		ing=(g>>3)+1; 
		inb=(b>>3)+1; 
		ind=(inr<<10)+(inr<<6)+inr+(ing<<5)+ing+inb;
		// [inr][ing][inb]
		++vwt[ind];
		vmr[ind] += r;
		vmg[ind] += g;
		vmb[ind] += b;
		m2[ind] += table[r]+table[g]+table[b];
	}

	if (!quan_sqrt) return;
	// "Diameter weighting" in action
	for (i = 0; i < 33 * 33 * 33; i++)
	{
		double d;
		if (!vwt[i]) continue;
		d = vwt[i];
		d = (vwt[i] = sqrt(d)) / d;
		vmr[i] *= d;
		vmg[i] *= d;
		vmb[i] *= d;
		m2[i] *= d;
	}
}

/* At conclusion of the histogram step, we can interpret
 *   wt[r][g][b] = sum over voxel of P(c)
 *   mr[r][g][b] = sum over voxel of r*P(c)  ,  similarly for mg, mb
 *   m2[r][g][b] = sum over voxel of c^2*P(c)
 * Actually each of these should be divided by 'size' to give the usual
 * interpretation of P() as ranging from 0 to 1, but we needn't do that here.
 */

/* We now convert histogram into moments so that we can rapidly calculate
 * the sums of the above quantities over any desired box.
 */


static void M3d(vwt, vmr, vmg, vmb)	// compute cumulative moments.
int *vwt, *vmr, *vmg, *vmb;
{
	register unsigned short int ind1, ind2;
	register unsigned char i, r, g, b;
	long int line, line_r, line_g, line_b, area[33], area_r[33], area_g[33], area_b[33];
	double line2, area2[33];

	for(r=1; r<=32; ++r)
	{
		for(i=0; i<=32; ++i) area2[i]=area[i]=area_r[i]=area_g[i]=area_b[i]=0;
		for(g=1; g<=32; ++g)
		{
			line2 = line = line_r = line_g = line_b = 0;
			for(b=1; b<=32; ++b)
			{
				ind1 = (r<<10) + (r<<6) + r + (g<<5) + g + b;	// [r][g][b]
				line += vwt[ind1];
				line_r += vmr[ind1]; 
				line_g += vmg[ind1]; 
				line_b += vmb[ind1];
				line2 += m2[ind1];
				area[b] += line;
				area_r[b] += line_r;
				area_g[b] += line_g;
				area_b[b] += line_b;
				area2[b] += line2;
				ind2 = ind1 - 1089; /* [r-1][g][b] */
				vwt[ind1] = vwt[ind2] + area[b];
				vmr[ind1] = vmr[ind2] + area_r[b];
				vmg[ind1] = vmg[ind2] + area_g[b];
				vmb[ind1] = vmb[ind2] + area_b[b];
				m2[ind1] = m2[ind2] + area2[b];
			}
		}
	}
}


static long int Vol(cube, mmt)			// Compute sum over a box of any given statistic
struct box *cube;
int mmt[33][33][33];
{
	return( mmt[cube->r1][cube->g1][cube->b1]
		-mmt[cube->r1][cube->g1][cube->b0]
		-mmt[cube->r1][cube->g0][cube->b1]
		+mmt[cube->r1][cube->g0][cube->b0]
		-mmt[cube->r0][cube->g1][cube->b1]
		+mmt[cube->r0][cube->g1][cube->b0]
		+mmt[cube->r0][cube->g0][cube->b1]
		-mmt[cube->r0][cube->g0][cube->b0] );
}

/* The next two routines allow a slightly more efficient calculation
 * of Vol() for a proposed subbox of a given box.  The sum of Top()
 * and Bottom() is the Vol() of a subbox split in the given direction
 * and with the specified new upper bound.
 */

static long int Bottom(cube, dir, mmt)
// Compute part of Vol(cube, mmt) that doesn't depend on r1, g1, or b1
// (depending on dir)
struct box *cube;
unsigned char dir;
int mmt[33][33][33];
{
	switch(dir)
	{
		case RED:
			return( -mmt[cube->r0][cube->g1][cube->b1]
				+mmt[cube->r0][cube->g1][cube->b0]
				+mmt[cube->r0][cube->g0][cube->b1]
				-mmt[cube->r0][cube->g0][cube->b0] );
			break;
		case GREEN:
			return( -mmt[cube->r1][cube->g0][cube->b1]
				+mmt[cube->r1][cube->g0][cube->b0]
				+mmt[cube->r0][cube->g0][cube->b1]
				-mmt[cube->r0][cube->g0][cube->b0] );
			break;
		case BLUE:
			return( -mmt[cube->r1][cube->g1][cube->b0]
				+mmt[cube->r1][cube->g0][cube->b0]
				+mmt[cube->r0][cube->g1][cube->b0]
				-mmt[cube->r0][cube->g0][cube->b0] );
			break;
	}
	return 0;
}


static long int Top(cube, dir, pos, mmt)
// Compute remainder of Vol(cube, mmt), substituting pos for
// r1, g1, or b1 (depending on dir)
struct box *cube;
unsigned char dir;
int pos;
int mmt[33][33][33];
{
	switch(dir)
	{
		case RED:
			return( mmt[pos][cube->g1][cube->b1] 
				-mmt[pos][cube->g1][cube->b0]
				-mmt[pos][cube->g0][cube->b1]
				+mmt[pos][cube->g0][cube->b0] );
			break;
		case GREEN:
			return( mmt[cube->r1][pos][cube->b1] 
				-mmt[cube->r1][pos][cube->b0]
				-mmt[cube->r0][pos][cube->b1]
				+mmt[cube->r0][pos][cube->b0] );
			break;
		case BLUE:
			return( mmt[cube->r1][cube->g1][pos]
				-mmt[cube->r1][cube->g0][pos]
				-mmt[cube->r0][cube->g1][pos]
				+mmt[cube->r0][cube->g0][pos] );
			break;
	}
	return 0;
}


static double Var(cube)
// Compute the weighted variance of a box
// NB: as with the raw statistics, this is really the variance * size
struct box *cube;
{
	double dr, dg, db, xx;

	dr = Vol(cube, mr); 
	dg = Vol(cube, mg); 
	db = Vol(cube, mb);
	xx =     m2[ 33*33*cube->r1 + 33*cube->g1 + cube->b1] 
		-m2[ 33*33*cube->r1 + 33*cube->g1 + cube->b0]
		-m2[ 33*33*cube->r1 + 33*cube->g0 + cube->b1]
		+m2[ 33*33*cube->r1 + 33*cube->g0 + cube->b0]
		-m2[ 33*33*cube->r0 + 33*cube->g1 + cube->b1]
		+m2[ 33*33*cube->r0 + 33*cube->g1 + cube->b0]
		+m2[ 33*33*cube->r0 + 33*cube->g0 + cube->b1]
		-m2[ 33*33*cube->r0 + 33*cube->g0 + cube->b0];
	return( xx - (dr*dr+dg*dg+db*db)/(double)Vol(cube,wt) );    
}

/* We want to minimize the sum of the variances of two subboxes.
 * The sum(c^2) terms can be ignored since their sum over both subboxes
 * is the same (the sum for the whole box) no matter where we split.
 * The remaining terms have a minus sign in the variance formula,
 * so we drop the minus sign and MAXIMIZE the sum of the two terms.
 */


static double Maximize(cube, dir, first, last, cut, whole_r, whole_g, whole_b, whole_w)
struct box *cube;
unsigned char dir;
int first, last, *cut;
long int whole_r, whole_g, whole_b, whole_w;
{
	register long int half_r, half_g, half_b, half_w;
	long int base_r, base_g, base_b, base_w;
	register int i;
	register double temp, max;

	base_r = Bottom(cube, dir, mr);
	base_g = Bottom(cube, dir, mg);
	base_b = Bottom(cube, dir, mb);
	base_w = Bottom(cube, dir, wt);
	max = 0.0;
	*cut = -1;

	for(i=first; i<last; ++i)
	{
		half_r = base_r + Top(cube, dir, i, mr);
		half_g = base_g + Top(cube, dir, i, mg);
		half_b = base_b + Top(cube, dir, i, mb);
		half_w = base_w + Top(cube, dir, i, wt);
		// now half_x is sum over lower half of box, if split at i
		if (half_w == 0)
		{				// subbox could be empty of pixels!
			continue;		// never split into an empty box
		}
		else temp = ((double)half_r*half_r + (double)half_g*half_g + (double)half_b*half_b)/half_w;

		half_r = whole_r - half_r;
		half_g = whole_g - half_g;
		half_b = whole_b - half_b;
		half_w = whole_w - half_w;
		if (half_w == 0)
		{				// subbox could be empty of pixels!
			continue;		// never split into an empty box
		}
		else temp += ((double)half_r*half_r + (double)half_g*half_g + (double)half_b*half_b)/half_w;

		if (temp > max)
		{
			max=temp;
			*cut=i;
		}
	}
	return(max);
}

static int Cut(struct box *set1, struct box *set2)
{
	unsigned char dir;
	int cutr, cutg, cutb;
	double maxr, maxg, maxb;
	long int whole_r, whole_g, whole_b, whole_w;

	whole_r = Vol(set1, mr);
	whole_g = Vol(set1, mg);
	whole_b = Vol(set1, mb);
	whole_w = Vol(set1, wt);

	maxr = Maximize(set1, RED, set1->r0+1, set1->r1, &cutr, whole_r, whole_g, whole_b, whole_w);
	maxg = Maximize(set1, GREEN, set1->g0+1, set1->g1, &cutg, whole_r, whole_g, whole_b, whole_w);
	maxb = Maximize(set1, BLUE, set1->b0+1, set1->b1, &cutb, whole_r, whole_g, whole_b, whole_w);

	if( (maxr>=maxg)&&(maxr>=maxb) )
	{
		dir = RED;
		if (cutr < 0) return 0;		// can't split the box
	}
	else
	if( (maxg>=maxr)&&(maxg>=maxb) ) 
		dir = GREEN;
	else
		dir = BLUE; 

	set2->r1 = set1->r1;
	set2->g1 = set1->g1;
	set2->b1 = set1->b1;

	switch (dir)
	{
		case RED:
			set2->r0 = set1->r1 = cutr;
			set2->g0 = set1->g0;
			set2->b0 = set1->b0;
			break;
		case GREEN:
			set2->g0 = set1->g1 = cutg;
			set2->r0 = set1->r0;
			set2->b0 = set1->b0;
			break;
		case BLUE:
			set2->b0 = set1->b1 = cutb;
			set2->r0 = set1->r0;
			set2->g0 = set1->g0;
			break;
	}
	set1->vol=(set1->r1-set1->r0)*(set1->g1-set1->g0)*(set1->b1-set1->b0);
	set2->vol=(set2->r1-set2->r0)*(set2->g1-set2->g0)*(set2->b1-set2->b0);

	return 1;
}


static void Mark(struct box *cube, int label, unsigned char *tag)
{
	register int r, g, b;

	for(r=cube->r0+1; r<=cube->r1; ++r)
		for(g=cube->g0+1; g<=cube->g1; ++g)
			for(b=cube->b0+1; b<=cube->b1; ++b)
				tag[(r<<10) + (r<<6) + r + (g<<5) + g + b] = label;
}

int wu_quant(unsigned char *inbuf, int width, int height, int quant_to, png_color *pal)
{
	void *mem;
	struct box	cube[MAXCOLOR];
	unsigned char	*tag;
	long int	next;
	register long int i, k, weight;
	double		vv[MAXCOLOR], temp;

	K = quant_to;
	size = width*height;

	mem = multialloc(MA_ALIGN_DOUBLE,
		&m2, 33*33*33 * sizeof(double),
		&wt, 33*33*33 * sizeof(int),
		&mr, 33*33*33 * sizeof(int),
		&mg, 33*33*33 * sizeof(int),
		&mb, 33*33*33 * sizeof(int),
		&tag, 33*33*33, NULL);
	if (!mem) return (-1);

	Hist3d(inbuf, wt, mr, mg, mb);
	M3d(wt, mr, mg, mb);

	cube[0].r0 = cube[0].g0 = cube[0].b0 = 0;
	cube[0].r1 = cube[0].g1 = cube[0].b1 = 32;
	next = 0;

	for(i=1; i<K; ++i)
	{
		if (Cut(&cube[next], &cube[i]))
		{		// volume test ensures we won't try to cut one-cell box
			vv[next] = (cube[next].vol>1) ? Var(&cube[next]) : 0.0;
			vv[i] = (cube[i].vol>1) ? Var(&cube[i]) : 0.0;
		}
		else
		{
			vv[next] = 0.0;		// don't try to split this box again
			i--;  	   		// didn't create box i
		}
		next = 0; temp = vv[0];
		for(k=1; k<=i; ++k)
			if (vv[k] > temp)
			{
				temp = vv[k];
				next = k;
			}

		if (temp <= 0.0)
		{
			K = i+1;		// Only got K boxes
			break;
		}
	}

	for(k=0; k<K; ++k)
	{
		Mark(&cube[k], k, tag);
		weight = Vol(&cube[k], wt);
		if (weight)
		{
			pal[k].red = Vol(&cube[k], mr) / weight;
			pal[k].green = Vol(&cube[k], mg) / weight;
			pal[k].blue = Vol(&cube[k], mb) / weight;
		}
		else pal[k].red = pal[k].green = pal[k].blue = 0;	// Bogus box
	}

	free(mem);
	return (0);
}
