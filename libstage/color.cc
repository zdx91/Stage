#include "stage.hh"
using namespace Stg;

#include "file_manager.hh"
#include <errno.h>

Color::Color( float r, float g, float b, float a )
  : r(r),g(g),b(b),a(a) 
{}

Color::Color() : 
  r(1.0), g(0.0), b(0.0), a(1.0) 
{}

bool Color::operator!=( const Color& other )
{
  double epsilon = 1e-4; // small
  return( fabs(r-other.r) > epsilon ||
		  fabs(g-other.g) > epsilon ||
		  fabs(b-other.b) > epsilon ||
		  fabs(a-other.a) > epsilon );
}

Color::Color( const char *name) :
  r(1), g(0), b(0), a(1)
{
  if( name == NULL ) // no string?
	return; // red
  
  if( strcmp( name, "" ) == 0 ) // empty string?
	return; // red
  
  static FILE *file = NULL;
  static GHashTable* table = g_hash_table_new( g_str_hash, g_str_equal );
  
  if( file == NULL )
	{
	  std::string rgbFile = FileManager::findFile( "rgb.txt" );
	  file = fopen( rgbFile.c_str(), "r" );
	  
	  if( file == NULL )
		{
		  PRINT_ERR1("unable to open color database: %s "
					 "(try adding rgb.txt's location to your STAGEPATH)",
					 strerror(errno));
		  exit(0);
		}
	  
	  PRINT_DEBUG( "Success!" );
	  
	  // load the file into the hash table       
	  while (TRUE)
		{
		  char line[1024];
		  if (!fgets(line, sizeof(line), file))
			break;

			// it's a macro or comment line - ignore the line
			// also ignore empty lines
			if (line[0] == '!' || line[0] == '#' || line[0] == '%' || line[0] == '\0') 
				continue;

			// Trim the trailing space
			while (strchr(" \t\n", line[strlen(line)-1]))
				line[strlen(line)-1] = 0;

			// Read the color
			int r, g, b;
			int chars_matched = 0;
			sscanf( line, "%d %d %d %n", &r, &g, &b, &chars_matched );
			
			Color* c = new Color( r / 255.0,
								  g / 255.0,
								  b / 255.0, 
								  1.0 );

			// Read the name
			char* colorname = strdup( line + chars_matched );

			// map the name to the color in the table
			g_hash_table_insert( table, (gpointer)colorname, (gpointer)c );
		}
		fclose(file);
	}
  
  // look up the colorname in the database  
  //stg_color_t* found = (stg_color_t*)g_hash_table_lookup( table, name );
  
  Color* found = (Color*)g_hash_table_lookup( table, name );  
  this->r = found->r;
  this->g = found->g;
  this->b = found->b;
  this->a = found->a;
}

bool Color::operator==( const Color& other )
{
  return( ! ((*this) != other) );
}

Color Color::RandomColor()
{
  return Color(  drand48(), drand48(), drand48() );
}

void Color::Print( const char* prefix )
{
  printf( "%s [%.2f %.2f %.2f %.2f]\n", prefix, r,g,b,a );
} 
