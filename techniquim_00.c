#include "basics.h"
#include <float.h>
#include <SDL.h>
#include <SDL_image.h>

#include "ok_lib.h"
#include "vec2d.h"
#include "transform.h"
#include "VFX.h"
#include "primitives.h"
#include "libcyaml/cyaml.h"
#include "open-simplex-noise.h"

SDL_Color lerp_through_array( Uint32 *palette, int palette_count, float amt ){
	SDL_Color out = {0,0,0,0};
	float step = 1.0 / (palette_count-1);
	for (int i = 1; i < palette_count; ++i ){
		if( amt < i * step ){
			out = lerp_SDL_Color( Uint23_to_SDL_Color( palette[i-1] ), 
								  Uint23_to_SDL_Color( palette[i] ), 
								  map( amt, (i-1)*step, i*step, 0, 1 ) );
			return out;
		}
	}
}


bool intersection( vec2d L0A, vec2d L0B, vec2d L1A, vec2d L1B ){

    float s1x, s1y, s2x, s2y;
    s1x = L0B.x - L0A.x;   s1y = L0B.y - L0A.y;
    s2x = L1B.x - L1A.x;   s2y = L1B.y - L1A.y;

    float s, t;
    s = (-s1y * (L0A.x - L1A.x) + s1x * (L0A.y - L1A.y)) / (-s2x * s1y + s1x * s2y);
    t = ( s2x * (L0A.y - L1A.y) - s2y * (L0A.x - L1A.x)) / (-s2x * s1y + s1x * s2y);

    //vec2d out = { NAN, NAN };
    if ( (s >= 0 && s <= 1) && (t >= 0 && t <= 1) ){
       //out.x = L0A.x + (t * s1x);
       //out.y = L0A.y + (t * s1y);
    	return 1;
    }
    return 0;
}


typedef struct wcoord_struct {
	int w [4];
} Wcoord;

Wcoord wc(int w0, int w1, int w2, int w3) { 
	Wcoord out;
	out.w[0] = w0; 
	out.w[1] = w1; 
	out.w[2] = w2; 
	out.w[3] = w3;
	return out;
}
Wcoord wc_sum( Wcoord A, Wcoord B ){
	Wcoord out;
	for (int i = 0; i < 4; i++) {
		out.w[i] = A.w[i] + B.w[i];
	}
	return out;
}
Wcoord wc_plus_warr( int *A, Wcoord B ){
	Wcoord out;
	for (int i = 0; i < 4; i++) {
		out.w[i] = A[i] + B.w[i];
	}
	return out;
}
Wcoord wc_scaled( int *A, int k ) {
	Wcoord out;
	for (int i = 0; i < 4; i++) {
		out.w[i] = A[i] * k;
	}
	return out;
}
vec2d wc_to_v2d( Wcoord A ){
	return v2d( A.w[0] + 0.5 * SQRT3 * A.w[1] + 0.5 * A.w[2], 
				0.5 * A.w[1] + 0.5 * SQRT3 * A.w[2] + A.w[3] );
}
vec2d warr_to_v2d( int *A ){
	return v2d( A[0] + 0.5 * SQRT3 * A[1] + 0.5 * A[2], 
				0.5 * A[1] + 0.5 * SQRT3 * A[2] + A[3] );
}
void sprint_wc( Wcoord A, char *buf ){
	sprintf( buf, "%d,%d,%d,%d", A.w[0], A.w[1], A.w[2], A.w[3] );
}

const cyaml_config_t cyamlconfig = {
	.log_fn = cyaml_log,            /* Use the default logging function. */
	.mem_fn = cyaml_mem,            /* Use the default memory allocator. */
	.log_level = CYAML_LOG_ERROR,   //CYAML_LOG_DEBUG,   // 
	.flags = CYAML_CFG_IGNORE_UNKNOWN_KEYS | CYAML_CFG_IGNORED_KEY_WARNING
};

struct config {

	char *tesselation_code;

	double scale;
	int AAx;

	char *palette;
	int palette_count;

	Uint32 edge_color;
	float edge_thickness;

	int halo_points;
	float halo_radius;
	float halo_strength;

	int frame_period;
};

const cyaml_schema_value_t color_schema = {
	CYAML_VALUE_UINT(CYAML_FLAG_DEFAULT, int)
};

static const cyaml_schema_field_t the_schema[] = {


	CYAML_FIELD_STRING_PTR( 
		"tesselation_code", CYAML_FLAG_POINTER_NULL_STR, struct config, tesselation_code, 0, INT32_MAX ),

	CYAML_FIELD_FLOAT( "scale", CYAML_FLAG_DEFAULT, struct config, scale ),
	CYAML_FIELD_UINT( "AA_Level", CYAML_FLAG_DEFAULT | CYAML_FLAG_OPTIONAL, struct config, AAx ),

	CYAML_FIELD_STRING_PTR( "palette", CYAML_FLAG_POINTER_NULL_STR, struct config, palette, 0, INT32_MAX ),

	CYAML_FIELD_UINT( "edge_color", CYAML_FLAG_DEFAULT, struct config, edge_color ),
	CYAML_FIELD_FLOAT( "edge_thickness", CYAML_FLAG_DEFAULT, struct config, edge_thickness ),

	CYAML_FIELD_UINT(  "halo_points", CYAML_FLAG_DEFAULT, struct config, halo_points ),
	CYAML_FIELD_FLOAT( "halo_radius", CYAML_FLAG_DEFAULT, struct config, halo_radius ),
	CYAML_FIELD_FLOAT( "halo_strength", CYAML_FLAG_DEFAULT, struct config, halo_strength ),

	CYAML_FIELD_UINT( "frame period", CYAML_FLAG_DEFAULT, struct config, frame_period ),
	CYAML_FIELD_END
};

static const cyaml_schema_value_t top_schema = {
	CYAML_VALUE_MAPPING( CYAML_FLAG_POINTER, struct config, the_schema ),
};


typedef struct{

	char *name;
	char *tags;
	int T1 [4];
	int T2 [4];
	int **seed;
	int seed_count;

} Tess;

static const cyaml_schema_value_t float_schema = {
	CYAML_VALUE_FLOAT(CYAML_FLAG_DEFAULT, float)
};
static const cyaml_schema_value_t int_schema = {
	CYAML_VALUE_INT(CYAML_FLAG_DEFAULT, int)
};
static const cyaml_schema_value_t Wcoord_schema = {
	CYAML_VALUE_SEQUENCE_FIXED( CYAML_FLAG_POINTER, int*, &int_schema, 4 )
};

const cyaml_schema_field_t Tess_fields[] = {

	CYAML_FIELD_STRING_PTR( "name", CYAML_FLAG_POINTER_NULL_STR, Tess, name, 0, INT32_MAX ),
	CYAML_FIELD_STRING_PTR( "tags", CYAML_FLAG_POINTER_NULL_STR | CYAML_FLAG_OPTIONAL, Tess, tags, 0, 8 ),
	CYAML_FIELD_SEQUENCE_FIXED( "T1", CYAML_FLAG_DEFAULT | CYAML_FLAG_FLOW, Tess, T1, &int_schema, 4),
	CYAML_FIELD_SEQUENCE_FIXED( "T2", CYAML_FLAG_DEFAULT | CYAML_FLAG_FLOW, Tess, T2, &int_schema, 4),
	CYAML_FIELD_SEQUENCE( "seed", CYAML_FLAG_POINTER, Tess, seed, &Wcoord_schema, 1, INT32_MAX ),
	CYAML_FIELD_END
};
const cyaml_schema_value_t Tess_value = {
	CYAML_VALUE_MAPPING( CYAML_FLAG_DEFAULT, Tess, Tess_fields )
};
const cyaml_schema_value_t Tess_seq_schema_value = {
	CYAML_VALUE_SEQUENCE( CYAML_FLAG_POINTER_NULL, Tess, &Tess_value, 0, INT32_MAX ) 
};



typedef struct{
	vec2d *V;
} geo;

typedef struct regpol{
	
	int sides;
	int angle;
	vec2d center;
	float quad_factor;
	geo *G;
	SDL_Color *color;

} regular_poly;

typedef struct ok_vec_of(regular_poly) regpolvec;

const double two_pi_over[] = {0, 6.283185307180, 3.141592653590, 2.094395102393, 1.570796326795, 1.256637061436, 1.047197551197, 0.897597901026, 0.785398163397, 0.698131700798, 0.628318530718, 0.571198664289, 0.523598775598, 0.483321946706, 0.448798950513, 0.418879020479, 0.392699081699};

//modf but good
double modfg(double x, double* intpart){
	double rx = round(x);
	if( fabs( rx - x ) < 0.0000000001 ) x = rx;
	return modf( x, intpart );
}

int breakdown_regpol_angle( int sides, double angle ){
	double alpha = two_pi_over[ sides ];
	int intpart = 0;
	//double mo = modfg( fabs(angle) / alpha, &intpart );
	//printf("breaking down %d: %lg / %lg = %lg. modf = %0.32lf <%d> (%d)\n", sides, fabs(angle), alpha, fabs(angle) / alpha, mo, mo == 1, intpart );
	
	switch( sides ){
		case 3:
			if( angle < 0 ) angle += TWO_THIRDS_PI;
			if( modfg(  angle             / alpha, &intpart ) < 0.01 ) return 0;
			if( modfg( (angle - SIXTH_PI) / alpha, &intpart ) < 0.01 ) return 1;
			if( modfg( (angle - THIRD_PI) / alpha, &intpart ) < 0.01 ) return 2;			
			if( modfg( (angle - HALF_PI)  / alpha, &intpart ) < 0.01 ) return 3;
			break;
		case 4:
			if( angle < 0 ) angle += HALF_PI;
			if( modfg( (angle - TWELFTH_PI  ) / alpha, &intpart ) < 0.01 ) return 0;
			if( modfg( (angle - QUARTER_PI  ) / alpha, &intpart ) < 0.01 ) return 1;			
			if( modfg( (angle - 5*TWELFTH_PI) / alpha, &intpart ) < 0.01 ) return 2;
			break;
		case 6:
			if( angle < 0 ) angle += THIRD_PI;
			if( modfg(  angle            / alpha, &intpart ) < 0.01 ) return 0;
			if( modfg( (angle - HALF_PI) / alpha, &intpart ) < 0.01 ) return 1;
			break;
		case 12:
			return 0;
	}
	printf("can't breakdown_regpol_angle( %d, %.12lg )!!!\n", sides, angle );
	return -1;
}
double angle_from_id( int sides, int angle_id ){
	switch( sides ){
		case 3:
			switch( angle_id ){
				case 0: return 0;
				case 1: return SIXTH_PI;
				case 2: return THIRD_PI;
				case 3: return HALF_PI;
			}
			break;
		case 4:
			switch( angle_id ){
				case 0: return TWELFTH_PI;
				case 1: return QUARTER_PI;
				case 2: return 5*TWELFTH_PI;
			}
			break;
		case 6:
			switch( angle_id ){
				case 0: return 0;
				case 1: return HALF_PI;
			}
			break;
		case 12:
			return TWELFTH_PI;
	}
	printf("weird angle( %d, %d )!!!\n", sides, angle_id );
	return 0;
}

typedef struct zvs ok_vec_of(regular_poly*) zonevec;

void paint_poly( SDL_Color *paint, vec2d mouse, zonevec *zones, int zone_cols, int AAx, SDL_Rect *bounds, float zone_iw, float zone_ih ){
	
	v2d_mult( &mouse, AAx );
	if( !coordinates_in_Rect( mouse.x, mouse.y, bounds ) ) return;

	int MI = (int)(( mouse.x - bounds->x) * zone_iw);
	int MJ = (int)(( mouse.y - bounds->y) * zone_ih);
	int MZ = MI + (MJ*zone_cols);
	//int zn = ok_vec_count( zones + MZ );
	//printf("___%d, %d. (%d)\n", MI, MJ, zn );
	vec2d mouseray = v2d( mouse.x + 1000, mouse.y+10 );
	v2d_mult( &mouseray, AAx );
	
	vec2d VT [12];
	ok_vec_foreach(zones + MZ, regular_poly *rp) {
	//for (int i = 0; i < zn; ++i ){
	//	regular_poly *rp = ok_vec_get(zones + MZ, i);
		for (int v = 0; v < rp->sides; ++v ){
			VT[v] = v2d( rp->center.x + rp->G->V[v].x, rp->center.y + rp->G->V[v].y );
		}
		int ic = 0;
		for (int v = 0; v < rp->sides-1; ++v ){
			ic += intersection( VT[v], VT[v+1], mouse, mouseray );
		}
		ic += intersection( VT[rp->sides-1], VT[0], mouse, mouseray );

		if( ic % 2 == 1 ){
			rp->color = paint;
			return;
		}
	}
}


void gp_quadpoly_mono( SDL_Renderer *R, int sides, vec2d center, vec2d *V, float quad_factor,
					   SDL_Color fill, SDL_Color stroke, float stroke_radius ){

	SDL_Vertex verts [24];
	for (int i = 0; i < sides; ++i ){
		verts[i] = (SDL_Vertex){ { center.x + V[i].x, center.y + V[i].y }, fill, {0,0} };
	}
	for (int i = sides; i < 2*sides; ++i ){
		verts[i] = (SDL_Vertex){ { center.x + quad_factor * V[i-sides].x, 
											center.y + quad_factor * V[i-sides].y }, fill, {0,0} };
	}
	int N = 6 * sides;
	int indices [72];
	for(int i = 0; i < sides; i++){
		indices[ 6*i   ] = i;
		indices[ 6*i+1 ] = i+sides;
		indices[ 6*i+2 ] = i+sides+1;

		indices[ 6*i+3 ] = i;
		indices[ 6*i+4 ] = i+sides+1;
		indices[ 6*i+5 ] = i+1;
	}
	indices[ N-4 ] = sides;
	indices[ N-2 ] = sides;
	indices[ N-1 ] = 0;

	if( SDL_RenderGeometry( R, NULL, verts, 2*sides, indices, N ) < 0 ){
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_RenderGeometry error: %s", SDL_GetError());
	}

	if( stroke_radius > 0 ){
		SDL_SetRenderDrawColor( R, stroke.r, stroke.g, stroke.b, stroke.a);
		int n = sides - 1;
		if( stroke_radius == 0.5 ){
			for (int i = 0; i < n; ++i){
				SDL_RenderDrawLineF( R, verts[i].position.x,   verts[i].position.y, 
										verts[i+1].position.x, verts[i+1].position.y );
			}
			SDL_RenderDrawLineF( R, verts[n].position.x, verts[n].position.y, 
									verts[0].position.x, verts[0].position.y );
		}
		else{
			for (int i = 0; i < n; ++i ){
				 gp_draw_thickLine( R, verts[i].position.x,   verts[i].position.y, 
				 					   verts[i+1].position.x, verts[i+1].position.y, stroke_radius );
				//gp_fill_fastcircle( R, verts[i].position.x,   verts[i].position.y,   stroke_radius );
			}
			 gp_draw_thickLine( R, verts[n].position.x, verts[n].position.y, 
			 					   verts[0].position.x, verts[0].position.y, stroke_radius );
			//gp_fill_fastcircle( R, verts[n].position.x, verts[n].position.y, stroke_radius );
		}
	}
}

void gp_quadpoly( SDL_Renderer *R, regular_poly *P, int offset ){

	if( P->quad_factor <= 0 || P->quad_factor >= 1 ) return;

	static const int indices [6] = { 0, 2, 3, 0, 3, 1 };

	for(int s = 0; s < P->sides; s++){

		int ns = s+1;
		if( ns >= P->sides ) ns = 0;

		SDL_Vertex verts [4];
		int C = (s + offset + P->angle) % P->sides;
		verts[0] = (SDL_Vertex){ { P->center.x +                  P->G->V[s ].x,  
											P->center.y +                  P->G->V[s ].y }, P->color[C], {0,0} };//(C+1)%sides
		verts[1] = (SDL_Vertex){ { P->center.x +                  P->G->V[ns].x, 
											P->center.y +                  P->G->V[ns].y }, P->color[C], {0,0} };//(C+2)%sides
		verts[2] = (SDL_Vertex){ { P->center.x + P->quad_factor * P->G->V[s ].x,  
											P->center.y + P->quad_factor * P->G->V[s ].y }, P->color[C], {0,0} };//(C+3)%sides
		verts[3] = (SDL_Vertex){ { P->center.x + P->quad_factor * P->G->V[ns].x, 
											P->center.y + P->quad_factor * P->G->V[ns].y }, P->color[C], {0,0} };//(C+4)%sides

		if( SDL_RenderGeometry( R, NULL, verts, 4, indices, 6 ) < 0 ){
			SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_RenderGeometry error: %s", SDL_GetError());
		}
	}
}


int export_svg( regpolvec *regpols, char *filename ){
	
	FILE *f = fopen( filename, "w" );

	fprintf(f, "<svg>\n\n" );

	int tri=0, tetra=0, hexa=0, dodec=0;

	fprintf(f, "   <g\n      inkscape:groupmode=\"layer\"\n      id=\"layer1\"\n      inkscape:label=\"bg\">\n" );
   fprintf(f, "      <rect\n         style=\"fill:#000000;stroke:none;\"\n         id=\"bg_rect\"\n         width=\"3240\"\n         height=\"2075\"\n         x=\"-240\"\n         y=\"-330\" />\n   </g>\n" );

	fprintf(f, "   <g\n      inkscape:groupmode=\"layer\"\n      id=\"layer2\"\n      inkscape:label=\"geometry\">\n" );

	ok_vec_foreach_ptr( regpols, regular_poly *rp ){

		for (int s = 0; s < rp->sides; s++ ){
			fprintf(f, "   <path\n" );
			switch( rp->sides ){
				case 3: fprintf(f, "      id=\"trigon-%d-face-%d\"\n", tri++, s ); break;
				case 4: fprintf(f, "      id=\"tetragon-%d-face-%d\"\n", tetra++, s ); break;
				case 6: fprintf(f, "      id=\"hexagon-%d-face-%d\"\n", hexa++, s ); break;
				case 12: fprintf(f, "      id=\"dodecgon-%d-face-%d\"\n", dodec++, s ); break;
			}
			fprintf(f, "      d=\"M " );
			int ns = s+1;
			if( ns >= rp->sides ) ns = 0;
			fprintf(f, "%lg,%lg ", rp->center.x +                  rp->G->V[s].x,  rp->center.y +                  rp->G->V[s].y  );
			fprintf(f, "%lg,%lg ", rp->center.x + rp->quad_factor * rp->G->V[s].x,  rp->center.y + rp->quad_factor * rp->G->V[s].y  );
			fprintf(f, "%lg,%lg ", rp->center.x + rp->quad_factor * rp->G->V[ns].x, rp->center.y + rp->quad_factor * rp->G->V[ns].y );
			fprintf(f, "%lg,%lg ", rp->center.x +                  rp->G->V[ns].x, rp->center.y +                  rp->G->V[ns].y );
			
			fprintf(f, "z\"\n" );

			fprintf(f, "      style=\"" );
			char buf [8];
			snprintf( buf, 7, "%08X", SDL_Color_to_Uint32(rp->color[s]) );
			fprintf(f, "fill:#%s;", buf );
			//snprintf( buf, 7, "%08X", OBJS[i].U.S.stroke_color );
			//if( OBJS[i].U.S.stroke ) fprintf(f, "stroke:#%s;", buf );
			fprintf(f, "stroke:none;" );
			//if( OBJS[i].U.S.type == 'L' ) fprintf(f, "stroke-width:%lg;stroke-linecap:round", OBJS[i].U.S.u.line.thickness);
			fprintf(f, "\" />\n" );
		}
	}

	fprintf(f, "   </g>\n</svg>" );
	fclose( f );
	return 0;
}






int main(int argc, char *argv[]){

	srand (time(NULL));
	char buf [256];

	//HWND hwnd_win = GetConsoleWindow();
	//ShowWindow(hwnd_win,SW_HIDE);
	SDL_Window *window;
	SDL_Renderer *rend;
	int width, height;
	bool loop = 1;
	vec2d mouse = v2dzero;
	vec2d pmouse = v2dzero;


	if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't initialize SDL: %s", SDL_GetError());
		return 3;
	}

	if (SDL_CreateWindowAndRenderer(1, 1, SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED, &window, &rend)) {
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't create window and renderer: %s", SDL_GetError());
		return 3;
	}
	SDL_SetRenderDrawBlendMode( rend, SDL_BLENDMODE_BLEND );
	//SDL_MaximizeWindow( window );
	SDL_SetWindowTitle( window, "Tecniquim 00" );
	SDL_GetWindowSize( window, &width, &height );
	int cx = width/2;
	int cy = height/2;

	IMG_Init(IMG_INIT_PNG);
	//SDL_Surface *icon = IMG_Load( "icon32.png" );
	//SDL_SetWindowIcon( window, icon );

	


	struct config *CFG;

	cyaml_err_t err = cyaml_load_file( "config.yaml", &cyamlconfig, &top_schema, 
									   (cyaml_data_t **)&CFG, NULL );
	if (err != CYAML_OK) {
		printf("CYAML ERROR: %d\n", err );
		abort();
	}

	
	SDL_Color edge_color = Uint32_to_SDL_Color( CFG->edge_color );
	CFG->edge_thickness = CFG->edge_thickness * CFG->AAx * 0.5;

	sprintf( buf, "data/%s", CFG->palette );
	SDL_Surface *palsurf = IMG_Load( buf );
	CFG->palette_count = palsurf->w;
	Uint32 *palpix = (Uint32*) palsurf->pixels;
	SDL_Color *palette = malloc( CFG->palette_count * sizeof(SDL_Color) );
	for (int i = 0; i < CFG->palette_count; ++i ){
		//printf("palpix[i]: %08X\n", palpix[i] );
		palette[i] = Uint32_to_SDL_Color( palpix[i] );
	}
	int palW = 30;
	int palY = (height - (CFG->palette_count * palW))/2;

	SDL_Color Z = {0,0,0,0};
	/*
		Uint32 tripal [3] =   { 0x019bcdff, 0x98a4b7ff, 0x252b44ff };
		Uint32 tetrapal [4] = { 0x047eb9ff, 0xdcdee1ff, 0x5f6d88ff, 0x1f2238ff };
		Uint32 hexapal [6] =  { 0x0a649dff, 0x56d3e1ff, 0xc7cbd5ff, 0x717f97ff, 0x363f5dff, 0x19192bff 
		for (int i = 0; i < 3; ++i ) tri_palette[i] = Uint23_to_SDL_Color( tripal[i] );
		for (int i = 0; i < 4; ++i ) tetra_palette[i] = Uint23_to_SDL_Color( tetrapal[i] );
		for (int i = 0; i < 6; ++i ) hexa_palette[i] = Uint23_to_SDL_Color( hexapal[i] );
	*/
	//int paln = 8;
	//Uint32 pal [8] = { 0x14191fFF, 0xb13cb1FF, 0x388bffFF, 0x57f487FF, 0xa8db1bFF, 0xdf962fFF, 0xd66553FF, 0xc35063FF };
	int paln = 2;
	Uint32 pal [2] = { 0x000000FF, 0xffffffFF };
	//int paln = 3;
	//Uint32 pal [3] = { 0x000000FF, 0xffffffFF, 0x000000FF };
	//int paln = 4;
	//Uint32 pal [4] = { 0x000000FF, 0xffffffFF, 0xffffffFF, 0x000000FF };
	//int paln = 12;
	//Uint32 pal [12] = {0xeaeaeaff, 0x151515ff, 0x808080ff, 0x151515ff, 0xeaeaeaff, 0x808080ff,
	//						 0xeaeaeaff, 0x151515ff, 0x808080ff, 0x151515ff, 0xeaeaeaff, 0x808080ff };


	SDL_Color tri_palette [3]; 
	SDL_Color tetra_palette [4];
	SDL_Color hexa_palette [6];
	SDL_Color dodec_palette[12];

	/*for (int i = 0; i < 12; ++i ){
		if( i < 3  ) tri_palette  [i] = Uint23_to_SDL_Color( pal[i] );
		if( i < 4  ) tetra_palette[i] = Uint23_to_SDL_Color( pal[i] );
		if( i < 6  ) hexa_palette [i] = Uint23_to_SDL_Color( pal[i] );
		if( i < 12 ) dodec_palette[i] = Uint23_to_SDL_Color( pal[i] );
	}*/
	
	for (int i = 0; i < 3; ++i ) tri_palette[i] = lerp_through_array( pal, paln, (1 + (2*i)) / 6.0 );
	for (int i = 0; i < 4; ++i ) tetra_palette[i] = lerp_through_array( pal, paln, (1 + (2*i)) / 8.0 );
	for (int i = 0; i < 6; ++i ) hexa_palette[i] = lerp_through_array( pal, paln, (1 + (2*i)) / 12.0 );
	for (int i = 0; i < 12; ++i ) dodec_palette[i] = lerp_through_array( pal, paln, (1 + (2*i)) / 24.0 );

	SDL_Color *current_paint = palette + 0;

	//SDL_Color fillA = Uint32_to_SDL_Color( CFG->color_fillA );
	//SDL_Color fillB = Uint32_to_SDL_Color( CFG->color_fillB );

	//printf("MM: %g\n", CFG->mouse_mass );

	bool panning = 0;
	bool pressed = 0;

	Transform T = (Transform){ 0, 0, 0, 0, 1, 1 };
	int scaleI = 0;
	set_scale( &T, CFG->scale * CFG->AAx );

	SDL_Rect bounds = (SDL_Rect){ -T.s, -T.s, 
							 (CFG->AAx * width)  + 2*(T.s), 
							 (CFG->AAx * height) + 2*(T.s) };
	//{ 50, 50, (CFG->AAx * width)-100, (CFG->AAx * height)-100 };

	vec2d T1, T2;
	int regpols_N = 0;
	
	regpolvec regpols;
	ok_vec_init(&regpols);

	typedef struct ok_vec_of(geo) geovec;
	geovec geov;
	ok_vec_init(&geov);
	ok_vec_ensure_capacity(&geov, 10);
	str_vec geo_codes;
	ok_vec_init(&geo_codes);
	typedef struct ok_map_of( const char*, geo* ) geomap;
	geomap geom; 
	ok_map_init(&geom);


	int zone_cols = 16;
	int zone_rows = 9;
	
	zonevec *zones = malloc( zone_rows * zone_cols * sizeof(zonevec) );
	for (int j = 0; j < zone_rows; ++j ){
		for (int i = 0; i < zone_cols; ++i ){
			ok_vec_init( zones + i + (j*zone_cols) );
		}
	}
	//these are inverses. we only ever need to divide by the w/h.
	float zone_iw = zone_cols / (float)bounds.w;
	float zone_ih = zone_rows / (float)bounds.h;


	float smallest_radius = 9999999;

	const double radii [] = { 0, 0, 0, 0.5773502691, 0.707106781186, 0, 1, 0, 0, 0, 0, 0, 1.93185165257 };

	err = 0;
	Uint32 tesselations_count = 0;
	Tess *tesselations = NULL;
	err |= cyaml_load_file( "data/tesselations.yaml", &cyamlconfig, &Tess_seq_schema_value, &tesselations, &tesselations_count );
	
	if( err != CYAML_OK ){
		printf("cyaml_load_file error: %s\n", cyaml_strerror(err) );
	}
	else{
		printf("tesselations:%d\n", tesselations_count );

		Tess *TT = NULL;
		if( strcmp( "RANDOM", CFG->tesselation_code ) == 0 ){

			int sel_count = 0;
			for (int i = 0; i < tesselations_count; ++i ){
				if( (strcchr( tesselations[i].tags, 'N' ) || //nice
					 strcchr( tesselations[i].tags, 'C' ) || //cool
					 strcchr( tesselations[i].tags, 'F' ))&& //fun
					 strcchr( tesselations[i].tags, 'B' ) == 0 ){// Bad

					sel_count++;
				}
			}
			int T = random(0, sel_count);
			for (int i = 0; i < tesselations_count; ++i ){
				if( (strcchr( tesselations[i].tags, 'N' ) || //nice
					 strcchr( tesselations[i].tags, 'C' ) || //cool
					 strcchr( tesselations[i].tags, 'F' ))&& //fun
					 strcchr( tesselations[i].tags, 'B' ) == 0 ){// Bad

					if( --T == 0 ){
						TT = tesselations + i;
						break;
					}
				}
			}
			//TT = tesselations + random(0, tesselations_count);
			int nl = strlen( TT->name );
			if( nl > 6 ){
				CFG->tesselation_code = realloc( CFG->tesselation_code, nl+1 );
			}
			sprintf( CFG->tesselation_code, "%s", TT->name );
		}
		else{
			for (int i = 0; i < tesselations_count; ++i ){
				if( strcmp( tesselations[i].name, CFG->tesselation_code ) == 0 ){
					TT = tesselations + i;
					printf("grabbing tesselations[%d]\n", i );
					break;
				}
			}
		}

		printf("TT: %s, seed_count: %d\n", TT->name, TT->seed_count );

		Wcoord dir12 [12];
		dir12[0] = wc(1, 0, 0, 0);
		dir12[1] = wc(0, 1, 0, 0);
		dir12[2] = wc(0, 0, 1, 0);
		dir12[3] = wc(0, 0, 0, 1);
		dir12[4] = wc(-1, 0, 1, 0);
		dir12[5] = wc(0, -1, 0, 1);
		dir12[6] = wc(-1, 0, 0, 0);
		dir12[7] = wc(0, -1, 0, 0);
		dir12[8] = wc(0, 0, -1, 0);
		dir12[9] = wc(0, 0, 0, -1);
		dir12[10] = wc(1, 0, -1, 0);
		dir12[11] = wc(0, 1, 0, -1);

		//                                2  3  4   5   
		const int polytype [] = { -1, -1, 3, 4, 6, 12 };

		map_str_int hash;
		ok_map_init( &hash );
		str_vec coord_codes;
		ok_vec_init(&coord_codes);

		vec2d bbmin = v2d( 999999,  999999);
		vec2d bbmax = v2d(-999999, -999999);
		for (int s = 0; s < TT->seed_count; s++) {
			vec2d v = warr_to_v2d( TT->seed[s] );
			if( v.x < bbmin.x ) bbmin.x = v.x;
			if( v.y < bbmin.y ) bbmin.y = v.y;
			if( v.x > bbmax.x ) bbmax.x = v.x;
			if( v.y > bbmax.y ) bbmax.y = v.y;
		}
		vec2d bb = v2d_diff( bbmax, bbmin );
		int WN = ceil( 6 * (width  / (bb.x * CFG->scale)) );
		int HN = ceil( 6 * (height / (bb.y * CFG->scale)) );
		//printf(">%d, %d\n", WN, HN );

		vec2d vT1 = warr_to_v2d( TT->T1 );
		vec2d vT2 = warr_to_v2d( TT->T2 );
		vec2d vtt = v2d_sum( vT1, vT2 );
		int tWN = ceil( 6 * (width  / (vtt.x * CFG->scale)) );
		int tHN = ceil( 6 * (height / (vtt.y * CFG->scale)) );
		//printf(">%d, %d\n", tWN, tHN );

		WN = max( WN, tWN );
		HN = max( HN, tHN );
		printf("WN:%d, HN:%d\n", WN, HN );
		if( WN < 24 ) WN = HN;
		if( HN < 24 ) HN = WN;
		if( WN < 24 ) WN = 24;
		if( HN < 24 ) HN = 24;
		printf("WN:%d, HN:%d\n", WN, HN );

		for ( int x = -WN; x < WN; x++ ) {
			for ( int y = -HN; y < HN; y++ ) {
				Wcoord trans = wc_sum( wc_scaled( TT->T1, x ), wc_scaled( TT->T2, y ) );
				for (int s = 0; s < TT->seed_count; s++) {
					Wcoord C = wc_plus_warr( TT->seed[s], trans );
					sprint_wc( C, buf );
					char *str = malloc( strlen(buf)+1 );
					strcpy( str, buf );
					ok_vec_push(&coord_codes, str);
					ok_map_put( &hash, *ok_vec_last(&coord_codes), (s+1) );
				}
			}
		}
		printf("ok_vec_count(&coord_codes): %d\n", ok_vec_count(&coord_codes) );

		for ( int x = -WN; x < WN; x++ ) {
			for ( int y = -HN; y < HN; y++ ) {
				//printf("\n%dx%d\n", x, y );
				Wcoord trans = wc_sum( wc_scaled( TT->T1, x ), wc_scaled( TT->T2, y ) );
				for (int s = 0; s < TT->seed_count; s++) {
					Wcoord C = wc_plus_warr( TT->seed[s], trans );
					int face = 0;
					int neighs [12];
					for ( int d = 0; d < 6; d++ ) {
						Wcoord neighbor = wc_sum( C, dir12[d] );
						sprint_wc( neighbor, buf );
						if( ok_map_get( &hash, buf ) ){
							//putchar('>');
							neighs[ face++ ] = d;
						}
					}

					for( int n = 0; n < face-1; n++ ){

						int diff = neighs[n+1] - neighs[n];
						int skip = 12 / polytype[diff];

						vec2d centroid = v2d(0,0);
						vec2d first = v2d(NAN,0);
						
						Wcoord fc = C;
						for ( int f = 0; f < 12; f += skip ) {
							Wcoord nfc = wc_sum( fc, dir12[ (neighs[n] + f) % 12 ] );
							vec2d F = wc_to_v2d( nfc );
							if( isnan(first.x) ){
								first = F;
							}
							v2d_add( &(centroid), F );
							fc = nfc;
						}
						v2d_mult( &(centroid), 1.0 / polytype[diff] );
						vec2d tcen = apply_transform_v2d( &(centroid), &T );

						if( coordinates_in_Rect( tcen.x, tcen.y, &bounds ) ){
							regular_poly *P = ok_vec_push_new(&regpols);
							P->sides = polytype[diff];
							P->center = tcen;
							//P->color = palette + 6;

							switch( P->sides ){
								case 3:
									P->color = tri_palette;
									break;
								case 4:
									P->color = tetra_palette;
									break;
								case 6:
									P->color = hexa_palette;
									break;
								case 12:
									P->color = dodec_palette;
									break;
								default:
									P->color = palette;
							}

							//printf("~ %d, %.12lg\n", P->sides, angle );
							double angle = v2d_heading( v2d_diff(first, centroid) );
							int angle_id = breakdown_regpol_angle( P->sides, angle );
							P->angle = angle_id;

							sprintf( buf, "%d:%d", P->sides, angle_id );
							geo* G = ok_map_get(&geom, buf);
							if( G == NULL ){
								//printf("neogeo: [%s]\n", buf );
								G = ok_vec_push_new(&geov);
								G->V = malloc( P->sides * sizeof(vec2d) );
								angle = angle_from_id( P->sides, angle_id );
								float radius = T.s * radii[ P->sides ];
								if( radius < smallest_radius ) smallest_radius = radius;
								for (int v = 0; v < P->sides; ++v ){
									double theta = angle + v * two_pi_over[ P->sides ];
									G->V[v] = v2d( radius*cos(theta), radius*sin(theta) );
								}
								char *str = malloc( strlen(buf)+1 );
								strcpy( str, buf );
								ok_vec_push(&geo_codes, str);
								ok_map_put( &geom, *ok_vec_last(&geo_codes), G );
							}
							P->G = G;
						}
					}
				}
			}
		}

		cyaml_free( &cyamlconfig, &Tess_seq_schema_value, tesselations, tesselations_count );

		regpols_N = ok_vec_count(&regpols);
		printf("regpols_N: %d\n", regpols_N );

		ok_map_deinit(&hash);
		ok_vec_foreach(&coord_codes, char *str){
			free(str);
		}
		ok_vec_deinit(&coord_codes);


		ok_vec_foreach_ptr(&regpols, regular_poly *P) {
			//Registering polygons into zones:
			int I = (int)((P->center.x - bounds.x) * zone_iw);
			int J = (int)((P->center.y - bounds.y) * zone_ih);
			float radius = T.s * radii[ P->sides ];
			//printf("%d = (%lg - %d) * %g\n", I, tcen.x, bounds.x, zone_iw );
			ok_vec_push(zones + I + (J*zone_cols), P);
			//fuzzying the zoning:
			// right
			int Ip = (int)((P->center.x + radius - bounds.x) * zone_iw);
			bool doIp = (Ip != I) && Ip < zone_cols;
			if( doIp ) ok_vec_push(zones + Ip + (J*zone_cols), P);
			// left
			int Im = (int)((P->center.x - radius - bounds.x) * zone_iw);
			bool doIm = (Im != I) && Im >= 0;
			if( doIm ) ok_vec_push(zones + Im + (J*zone_cols), P);
			// below
			int Jp = (int)((P->center.y + radius - bounds.y) * zone_ih);
			bool doJp = (Jp != J) && Jp < zone_rows;
			if( doJp ) ok_vec_push(zones + I + (Jp * zone_cols), P);
			// above
			int Jm = (int)((P->center.y - radius - bounds.y) * zone_ih);
			bool doJm = (Jm != J) && Jm >= 0;
			if( doJm ) ok_vec_push(zones + I + (Jm * zone_cols), P);
			// pp
			if( doIp && doJp ) ok_vec_push(zones + Ip + (Jp * zone_cols), P);
			// pm
			if( doIp && doJm ) ok_vec_push(zones + Ip + (Jm * zone_cols), P);
			// mp
			if( doIm && doJp ) ok_vec_push(zones + Im + (Jp * zone_cols), P);
			// mm
			if( doIm && doJm ) ok_vec_push(zones + Im + (Jm * zone_cols), P);
		}

		int ztotal = 0;
		for (int j = 0; j < zone_rows; ++j ){
			for (int i = 0; i < zone_cols; ++i ){
				int Z = ok_vec_count( zones + i + (j*zone_cols) );
				//printf("%02d, ", Z );
				ztotal += Z;
			}
			//puts("");
		}
		printf("ztotal: %d\n", ztotal );
	}


	float halo_radius = smallest_radius * CFG->halo_radius;
	vec2d *halo_offsets = NULL;
	if( CFG->halo_points > 0 ){	
		halo_offsets = malloc( CFG->halo_points * sizeof(vec2d) );
		float halo_alpha = TWO_PI / CFG->halo_points;
		for (int i = 0; i < CFG->halo_points; ++i ){
			halo_offsets[i] = v2d( halo_radius * cos(i * halo_alpha), halo_radius * sin(i * halo_alpha) );
		}
	}


	struct osn_context *ctx;
	open_simplex_noise( rand(), &ctx );
	double nscale = 0.0005;
	double nx = 0;
	double ny = 0;
	//puts("created noise context");//debug

	vec2d bcenter = v2d( lerp( bounds.x, bounds.x+bounds.w, 0.5), lerp( bounds.y, bounds.y+bounds.h, 0.5) );
	double max_dist = hypot( bcenter.x - bounds.x, bcenter.y - bounds.y );

	ok_vec_foreach_ptr(&regpols, regular_poly *rp){
		
		//noise	
		rp->quad_factor = open_simplex_noise2d( ctx, rp->center.x * nscale + nx, rp->center.y * nscale + ny );
		rp->quad_factor = constrainF( rp->quad_factor + 0.5, 0, 1 );

		// linear
		//rp->quad_factor = constrainF( map( rp->center.x, bounds.x, bounds.x+bounds.w, 1.5, -0.5 ), 0.0001, 1);

		// radial
		//rp->quad_factor = constrainF( map( v2d_dist(rp->center, bcenter), 0, max_dist, 1.5, -0.5 ), 0.0001, 1);

		// constant thickness:
		//rp->quad_factor = 1 - (25 / (T.s * radii[ rp->sides ]));
	}

	//SDL_Rect screen_rct = (SDL_Rect){0,0,width,height};
	//tela_abaulada TA;
	//build_tela_abaulada( &TA, 64, &screen_rct, 0.4 );


	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
	SDL_Texture *AAtexture = SDL_CreateTexture( rend, SDL_PIXELFORMAT_RGBA8888, 
												SDL_TEXTUREACCESS_TARGET,  CFG->AAx * width, CFG->AAx * height );
	SDL_Rect AAdst = (SDL_Rect){ 0, 0, width, height };

	int framecount = 0;

	int dragging = 0;


	puts("<<Entering Main Loop>>");
	while ( loop ) {//============================================================================================================

		SDL_Event event;
		while( SDL_PollEvent(&event) ){

			switch (event.type) {
				case SDL_QUIT:
					goto exit;
					break;

				case SDL_RENDER_TARGETS_RESET:
					break;

				case SDL_KEYDOWN:
					break;

				case SDL_KEYUP:

					if( event.key.keysym.sym == 'e' ){
						time_t rawtime;
						struct tm * timeinfo;
						time ( &rawtime );
						timeinfo = localtime ( &rawtime );
						strftime( buf, 255, "export %Y.%m.%d %H-%M-%S.svg", timeinfo );
						printf("exporting \"%s\"!\n", buf );
						export_svg( &regpols, buf );
					}

					break;

				case SDL_MOUSEMOTION:

					pmouse = mouse;
					mouse.x = event.motion.x;
					mouse.y = event.motion.y;

					/*
					if( pressed ){
						paint_poly( current_paint, mouse, zones, zone_cols, CFG->AAx, &bounds, zone_iw, zone_ih );
						vec2d delta = v2d_diff( mouse, pmouse );
						double deltamag = v2d_mag( delta );
						if( deltamag > halo_radius ){
							int steps = floor(deltamag / halo_radius);
							vec2d step = v2d_setlen( delta, halo_radius );
							for (int s = 1; s <= steps; ++s ){
								vec2d v = v2d_sum( pmouse, v2d_product( step, s ) );
								paint_poly( current_paint, v, zones, zone_cols, CFG->AAx, &bounds, zone_iw, zone_ih );
							}
						}
					}
					*/

					if( dragging ){
						nx += 0.001 * (pmouse.x - mouse.x);
						ny += 0.001 * (pmouse.y - mouse.y);
					}

					break;
				case SDL_MOUSEBUTTONDOWN:
					/*
					if( event.button.button == SDL_BUTTON_LEFT  ){
						bool pickingcolor = 0;
						if( mouse.x > width - palW   &&   mouse.y > palY ){
							for (int i = 0; i < CFG->palette_count; ++i ){
								if( mouse.y < palY + (i+1) * palW ){
									current_paint = palette + i;
									pickingcolor = 1;
									break;
								}
							}
						}
						if( !pickingcolor ){
							paint_poly( current_paint, mouse, zones, zone_cols, CFG->AAx, &bounds, zone_iw, zone_ih );
							pressed = 1;
						}
					}*/

					if( event.button.button == SDL_BUTTON_RIGHT ){
						dragging = 1;
					}

					break;
				case SDL_MOUSEBUTTONUP:
					pressed = 0;
					dragging = 0;
					break;
				case SDL_MOUSEWHEEL:;
					/*
					double xrd = (mouse.x - T.cx) * T.invs;
					double yrd = (mouse.y - T.cy) * T.invs;
					scaleI -= event.wheel.y; //I = constrain(I, minI, maxI);
					set_scale( &T, pow(1.1, scaleI) );
					T.cx = mouse.x - xrd * T.s;
					T.cy = mouse.y - yrd * T.s;
					*/
					if( event.wheel.y < 0 ){
						nscale *= pow( 1.1, -event.wheel.y );
					}
					else{
						nscale *= pow( 0.9, event.wheel.y );
					}
					break;
			}
		}


		/*vec2d aam = v2d_product( mouse, 2 );
		ok_vec_foreach_ptr(&regpols, regular_poly *rp){
			rp->quad_factor = sq( sin( (v2d_dist( rp->center, aam ) + framecount) * 0.003 ) );
		}*/

		//nx += 0.0001 * (mouse.x - cx);
		//ny += 0.0001 * (mouse.y - cy);
		//nscale = map( mouse.x, 0, width, 0.005, 0.00001 );
		
		//*
		ok_vec_foreach_ptr(&regpols, regular_poly *rp){		
			rp->quad_factor = open_simplex_noise2d( ctx, rp->center.x * nscale + nx, rp->center.y * nscale + ny );
			rp->quad_factor = constrainF( rp->quad_factor + 0.5, 0.0001, 1 );
		}//*/


		SDL_SetRenderTarget( rend, AAtexture );
		//SDL_SetRenderDraw_Uint32( rend, CFG->color_background );
		SDL_SetRenderDrawColor( rend, 0,0,0,255 );
		SDL_RenderClear( rend );

		ok_vec_foreach_ptr(&regpols, regular_poly *rp){

			//double a = atan2( rp->center.y - (2*mouse.y), rp->center.x - (2*mouse.x) );
			//int offset = lrint( map( a, -PI, PI, rp->sides + 0.499, -0.499 ) );
			gp_quadpoly( rend, rp, 0 );//rp->angle
			//, edge_color, CFG->edge_thickness
		}

		SDL_SetRenderTarget( rend, NULL );
		
		SDL_RenderCopy( rend, AAtexture, NULL, &AAdst );

		//render_tela_abaulada( rend, AAtexture, &TA );

		/*for (int i = 0; i < CFG->palette_count; ++i ){
			SDL_Rect dst = (SDL_Rect){ width-palW, palY + i * palW, palW, palW };
			SDL_SetRenderDraw_SDL_Color( rend, palette + i );
			SDL_RenderFillRect( rend, &dst );
		}*/

		SDL_RenderPresent(rend);
		SDL_framerateDelay( CFG->frame_period );
		framecount++;
	}

	exit:;

	SDL_DestroyRenderer(rend);
	SDL_DestroyWindow(window);

	SDL_Quit();

	return 0;
}