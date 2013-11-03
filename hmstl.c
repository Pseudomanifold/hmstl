#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "heightmap.h"

typedef struct {
	int log; // boolean; verbose logging if true
	int base; // boolean; output walls and bottom as well as terrain surface if true
	int ascii; // boolean; output ASCII STL instead of binary STL if true
	char *input; // path to input file; use stdin if NULL
	char *output; // path to output file; use stdout if NULL
	float zscale; // scaling factor applied to raw Z values; default 1.0
	float baseheight; // height in STL units of base below lowest terrain (technically, offset added to scaled Z values); default and minimum 1.0
} Settings;
Settings CONFIG = {0, 1, 0, NULL, NULL, 1.0, 1.0};

void StartSTL(FILE *fp, const Heightmap *hm) {
	if (CONFIG.ascii) {
		fprintf(fp, "solid hmstl\n");
	}
	else {
		// Binary STL header consists of 80 evidently arbitrary bytes,
		// followed by a four byte unsigned long int representing the
		// number of triangles in the file. We should be able to pre-
		// compute this count based on the resolution and base settings.
		
		char *header;
		unsigned long facecount;
		
		header = (char *)calloc(1, 80);
		fwrite(header, 80, 1, fp);
		free(header);
		
		facecount = 2 * (hm->width - 1) * (hm->height - 1);
		if (CONFIG.base) {
			facecount += (4 * hm->width) + (4 * hm->height) - 6;
		}
		
		fwrite(&facecount, 4, 1, fp);
	}
}

void EndSTL(FILE *fp) {
	if (CONFIG.ascii) {
		fprintf(fp, "endsolid hmstl\n");
	}
}

void TriangleASCII(FILE *fp,
		float nx, float ny, float nz,
		float x1, float y1, float z1,
		float x2, float y2, float z2,
		float x3, float y3, float z3) {
	
	fprintf(fp, "facet normal %f %f %f\n", nx, ny, nz);
	fprintf(fp, "outer loop\n");
	fprintf(fp, "vertex %f %f %f\n", x1, y1, z1);
	fprintf(fp, "vertex %f %f %f\n", x2, y2, z2);
	fprintf(fp, "vertex %f %f %f\n", x3, y3, z3);
	fprintf(fp, "endloop\n");
	fprintf(fp, "endfacet\n");
}

void TriangleBinary(FILE *fp,
		float nx, float ny, float nz,
		float x1, float y1, float z1,
		float x2, float y2, float z2,
		float x3, float y3, float z3) {
	
	unsigned short attr = 0;
	
	fwrite(&nx, 4, 1, fp);
	fwrite(&ny, 4, 1, fp);
	fwrite(&nz, 4, 1, fp);
	
	fwrite(&x1, 4, 1, fp);
	fwrite(&y1, 4, 1, fp);
	fwrite(&z1, 4, 1, fp);
	
	fwrite(&x2, 4, 1, fp);
	fwrite(&y2, 4, 1, fp);
	fwrite(&z2, 4, 1, fp);
	
	fwrite(&x3, 4, 1, fp);
	fwrite(&y3, 4, 1, fp);
	fwrite(&z3, 4, 1, fp);
	
	fwrite(&attr, 2, 1, fp);
}

void Triangle(FILE *fp, const Heightmap *hm,
		unsigned int x1i, unsigned int y1i, float z1,
		unsigned int x2i, unsigned int y2i, float z2,
		unsigned int x3i, unsigned int y3i, float z3) {
	
	float x1, y1, x2, y2, x3, y3;
	float nx, ny, nz;
	
	x1 = (float)x1i; y1 = (float)(hm->height - y1i);
	x2 = (float)x2i; y2 = (float)(hm->height - y2i);
	x3 = (float)x3i; y3 = (float)(hm->height - y3i);
	
	// imply normals from face winding
	nx = 0; ny = 0; nz = 0;
	
	if (CONFIG.ascii) {
		TriangleASCII(fp, nx, ny, nz, x1, y1, z1, x2, y2, z2, x3, y3, z3);
	} else {
		TriangleBinary(fp, nx, ny, nz, x1, y1, z1, x2, y2, z2, x3, y3, z3);
	}
}

void Mesh(const Heightmap *hm, FILE *fp) {

	unsigned int row, col;
	unsigned int Ax, Ay, Bx, By, Cx, Cy, Dx, Dy;
	unsigned long A, B, C, D;
	
	for (row = 0; row < hm->height - 1; row++) {
		for (col = 0; col < hm->width - 1; col++) {
			
			/*
			 * Point a is at coordinates row, col.
			 * We output the quad between point A and C as two
			 * triangles, ABD and BCD.
			 * 
			 * A-D
			 * |/|
			 * B-C
			 * 
			 */
			
			// Vertex coordinates in terms of row and col.
			Ax = col;     Ay = row;
			Bx = col;     By = row + 1;
			Cx = col + 1; Cy = row + 1;
			Dx = col + 1; Dy = row;
			
			// Bitmap indices of vertex coordinates.
			A = (Ay * hm->width) + Ax;
			B = (By * hm->width) + Bx;
			C = (Cy * hm->width) + Cx;
			D = (Dy * hm->width) + Dx;
			
			// hm->data[N] is the raw Z value of vertex N.
			// Z values are not necessarily expressed in same units as XY.
			// We should scale to 1.0 accordingly, then apply any extra factor.
			
			// ABD
			Triangle(fp, hm,
					Ax, Ay, CONFIG.baseheight + CONFIG.zscale * hm->data[A],
					Bx, By, CONFIG.baseheight + CONFIG.zscale * hm->data[B],
					Dx, Dy, CONFIG.baseheight + CONFIG.zscale * hm->data[D]);
			
			// BCD
			Triangle(fp, hm,
					Bx, By, CONFIG.baseheight + CONFIG.zscale * hm->data[B],
					Cx, Cy, CONFIG.baseheight + CONFIG.zscale * hm->data[C],
					Dx, Dy, CONFIG.baseheight + CONFIG.zscale * hm->data[D]);
		}
	}
}

void Walls(const Heightmap *hm, FILE *fp) {
	unsigned int row, col;
	unsigned long a, b;
	unsigned int bottom = hm->height -1;
	unsigned int right = hm->width - 1;
	
	// north and south walls
	for (col = 0; col < hm->width - 1; col++) {
		
		// north wall
		a = col;
		b = col + 1;
		Triangle(fp, hm,
				a, 0, CONFIG.baseheight + CONFIG.zscale * hm->data[a],
				b, 0, CONFIG.baseheight + CONFIG.zscale * hm->data[b],
				a, 0, 0);
		Triangle(fp, hm,
				b, 0, CONFIG.baseheight + CONFIG.zscale * hm->data[b],
				b, 0, 0,
				a, 0, 0);
		
		// south wall
		a += bottom * hm->width;
		b += bottom * hm->width;
		Triangle(fp, hm,
				col, bottom, CONFIG.baseheight + CONFIG.zscale * hm->data[a],
				col, bottom, 0,
				col + 1, bottom, CONFIG.baseheight + CONFIG.zscale * hm->data[b]);
		Triangle(fp, hm,
				col, bottom, 0,
				col + 1, bottom, 0,
				col + 1, bottom, CONFIG.baseheight + CONFIG.zscale * hm->data[b]);
	}
	
	// west and east walls
	for (row = 0; row < hm->height - 1; row++) {
		
		// west wall
		a = row * hm->width;
		b = (row + 1) * hm->width;
		Triangle(fp, hm,
				0, row, CONFIG.baseheight + CONFIG.zscale * hm->data[a],
				0, row, 0,
				0, row + 1, CONFIG.baseheight + CONFIG.zscale * hm->data[b]);
		Triangle(fp, hm,
				0, row, 0,
				0, row + 1, 0,
				0, row + 1, CONFIG.baseheight + CONFIG.zscale * hm->data[b]);
		
		// east wall
		a += right;
		b += right;
		Triangle(fp, hm,
				right, row, CONFIG.baseheight + CONFIG.zscale * hm->data[a],
				right, row + 1, 0,
				right, row, 0);
		Triangle(fp, hm,
				right, row, CONFIG.baseheight + CONFIG.zscale * hm->data[a],
				right, row + 1, CONFIG.baseheight + CONFIG.zscale * hm->data[b],
				right, row + 1, 0);
	}
	
}

void Bottom(const Heightmap *hm, FILE *fp) {
	// Technically this may yield an invalid STL, since border
	// triangles will meet the edges of these bottom cap faces
	// in a series of T-junctions.
	Triangle(fp, hm,
			0, 0, 0,
			hm->width - 1, 0, 0,
			0, hm->height - 1, 0);
	Triangle(fp, hm,
			hm->width - 1, 0, 0,
			hm->width - 1, hm->height - 1, 0,
			0, hm->height - 1, 0);
}

// returns 0 on success, nonzero otherwise
int HeightmapToSTL(Heightmap *hm) {
	
	FILE *fp;
	
	if (CONFIG.output == NULL) {
		fp = stdout;
	}
	else {
		if ((fp = fopen(CONFIG.output, "w")) == NULL) {
			fprintf(stderr, "Cannot open output file %s\n", CONFIG.output);
			return 1;
		}
	}
		
	StartSTL(fp, hm);
	
	Mesh(hm, fp);
	
	if (CONFIG.base) {
		Walls(hm, fp);
		Bottom(hm, fp);
	}
	
	EndSTL(fp);
	
	if (fp != stdout) {
		fclose(fp);
	}
	
	return 0;
}

// https://www.gnu.org/software/libc/manual/html_node/Example-of-Getopt.html
// returns 0 if options are parsed successfully; nonzero otherwise
int parseopts(int argc, char **argv) {
	
	int c;
	
	// suppress automatic error messages generated by getopt
	opterr = 0;
	
	while ((c = getopt(argc, argv, "az:b:o:i:mv")) != -1) {
		switch (c) {
			case 'a':
				// ASCII mode output
				CONFIG.ascii = 1;
				break;
			case 'z':
				// Z scale (heightmap value units relative to XY)
				if (sscanf(optarg, "%f", &CONFIG.zscale) != 1 || CONFIG.zscale <= 0) {
					fprintf(stderr, "CONFIG.zscale must be a number greater than 0.\n");
					return 1;
				}
				break;
			case 'b':
				// Base height (default 1.0 units)
				if (sscanf(optarg, "%f", &CONFIG.baseheight) != 1 || CONFIG.baseheight < 1) {
					fprintf(stderr, "BASEHEIGHT must be a number greater than or equal to 1.\n");
					return 1;
				}
				break;
			case 'o':
				// Output file (default stdout)
				CONFIG.output = optarg;
				break;
			case 'i':
				// Input file (default stdin)
				CONFIG.input = optarg;
				break;
			case 'm':
				// mesh only mode - omit base (walls and bottom)
				CONFIG.base = 0;
				break;
			case 'v':
				// Verbose mode (log to stderr)
				CONFIG.log = 1;
				break;
			case '?':
				// unrecognized option OR missing option argument
				switch (optopt) {
					case 'z':
					case 'b':
					case 'i':
					case 'o':
						fprintf(stderr, "Option -%c requires an argument.\n", optopt);
						break;
					default:
						if (isprint(optopt)) {
							fprintf(stderr, "Unknown option -%c\n", optopt);
						}
						else {
							fprintf(stderr, "Unknown option character \\x%x\n", optopt);
						}
						break;
				}
				return 1;
				break;
			default:
				// My understand is getopt will return ? for all unrecognized options,
				// so I'm not sure what other cases will be caught here. Perhaps just
				// options specified in optstring that I forget to handle above...
				fprintf(stderr, "Unexpected getopt result: %c\n", optopt);
				return 1;
				break;
		}
	}
	
	// should be no parameters left
	if (optind < argc) {
		fprintf(stderr, "Extraneous arguments\n");
		return 1;
	}
	
	return 0;
}

int main(int argc, char **argv) {
	Heightmap *hm = NULL;
	
	if (parseopts(argc, argv)) {
		fprintf(stderr, "Usage: %s [-z CONFIG.zscale] [-b BASEHEIGHT] [-i INPUT] [-o OUTPUT]\n", argv[0]);
		return 1;
	}
	
	if ((hm = ReadHeightmap(CONFIG.input)) == NULL) {
		return 1;
	}
	
	if (CONFIG.log) {
		DumpHeightmap(hm);
	}
	
	HeightmapToSTL(hm);
	
	FreeHeightmap(hm);
	
	return 0;
}
