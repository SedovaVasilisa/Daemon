/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.
Copyright (C) 2006-2011 Robert Beckebans <trebor_7@users.sourceforge.net>
Copyright (C) 2009 Peter McNeill <n27@bigpond.net.au>

This file is part of Daemon source code.

Daemon source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Daemon source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Daemon source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
// tr_bsp.c
#include "tr_local.h"
#include "framework/CommandSystem.h"

/*
========================================================
Loads and prepares a map file for scene rendering.

A single entry point:
RE_LoadWorldMap(const char *name);
========================================================
*/

static world_t    s_worldData;
static int        s_lightCount;
static growList_t s_interactions;
static byte       *fileBase;

static int        c_redundantInteractions;
static int        c_vboWorldSurfaces;
static int        c_vboLightSurfaces;
static int        c_vboShadowSurfaces;

//===============================================================================

/*
===============
R_ColorShiftLightingBytes
===============
*/
static void R_ColorShiftLightingBytes( byte in[ 4 ], byte out[ 4 ] )
{
	int shift, r, g, b;

	/* Shift the color data based on overbright range.

	Historically the shift was:

	  shift = tr.mapOverBrightBits - tr.overbrightBits;

	But in Dæmon engine tr.overbrightBits is always zero
	as this value is zero when there hardware overbright
	bit is disabled, and the engine doesn't implement
	hardware overbright bit at all.

	The original code was there to only shift in software
	what hardware overbright bit feature was not doing, but
	this implementation is entirely software. */
	shift = tr.mapOverBrightBits;

	// shift the data based on overbright range
	r = in[ 0 ] << shift;
	g = in[ 1 ] << shift;
	b = in[ 2 ] << shift;

	// normalize by color instead of saturating to white
	if ( ( r | g | b ) > 255 )
	{
		int max;

		max = r > g ? r : g;
		max = max > b ? max : b;
		r = r * 255 / max;
		g = g * 255 / max;
		b = b * 255 / max;
	}

	out[ 0 ] = r;
	out[ 1 ] = g;
	out[ 2 ] = b;
	out[ 3 ] = in[ 3 ];
}

static void R_ColorShiftLightingBytesCompressed( byte in[ 8 ], byte out[ 8 ] )
{
	unsigned short rgb565;
	byte rgba[4];

	// color shift the endpoint colors in the dxt block
	rgb565 = in[1] << 8 | in[0];
	rgba[0] = (rgb565 >> 8) & 0xf8;
	rgba[1] = (rgb565 >> 3) & 0xfc;
	rgba[2] = (rgb565 << 3) & 0xf8;
	rgba[3] = 0xff;
	R_ColorShiftLightingBytes( rgba, rgba );
	rgb565 = ((rgba[0] >> 3) << 11) |
		((rgba[1] >> 2) << 5) |
		((rgba[2] >> 3) << 0);
	out[0] = rgb565 & 0xff;
	out[1] = rgb565 >> 8;

	rgb565 = in[3] << 8 | in[2];
	rgba[0] = (rgb565 >> 8) & 0xf8;
	rgba[1] = (rgb565 >> 3) & 0xfc;
	rgba[2] = (rgb565 << 3) & 0xf8;
	rgba[3] = 0xff;
	R_ColorShiftLightingBytes( rgba, rgba );
	rgb565 = ((rgba[0] >> 3) << 11) |
		((rgba[1] >> 2) << 5) |
		((rgba[2] >> 3) << 0);
	out[2] = rgb565 & 0xff;
	out[3] = rgb565 >> 8;
}

/*
===============
R_ProcessLightmap

        returns maxIntensity
===============
*/
float R_ProcessLightmap( byte *pic, int in_padding, int width, int height, int bits, byte *pic_out )
{
	int   j;
	float maxIntensity = 0;

	if( bits & IF_BC1 ) {
		for ( j = 0; j < ((width + 3) >> 2) * ((height + 3) >> 2); j++ )
		{
			R_ColorShiftLightingBytesCompressed( &pic[ j * 8 ], &pic_out[ j * 8 ] );
		}
	} else if( bits & (IF_BC2 | IF_BC3) ) {
		for ( j = 0; j < ((width + 3) >> 2) * ((height + 3) >> 2); j++ )
		{
			R_ColorShiftLightingBytesCompressed( &pic[ j * 16 ], &pic_out[ j * 16 ] );
		}
	} else {
		for ( j = 0; j < width * height; j++ )
		{
			R_ColorShiftLightingBytes( &pic[ j * in_padding ], &pic_out[ j * 4 ] );
			pic_out[ j * 4 + 3 ] = 255;
		}
	}

	return maxIntensity;
}

static int LightmapNameCompare( const char *s1, const char *s2 )
{
	int  c1, c2;

	do
	{
		c1 = *s1++;
		c2 = *s2++;

		if ( c1 >= 'a' && c1 <= 'z' )
		{
			c1 -= ( 'a' - 'A' );
		}

		if ( c2 >= 'a' && c2 <= 'z' )
		{
			c2 -= ( 'a' - 'A' );
		}

		if ( c1 == '\\' || c1 == ':' )
		{
			c1 = '/';
		}

		if ( c2 == '\\' || c2 == ':' )
		{
			c2 = '/';
		}

		if ( c1 < c2 )
		{
			// strings not equal
			return -1;
		}

		if ( c1 > c2 )
		{
			return 1;
		}
	}
	while ( c1 );

	// strings are equal
	return 0;
}

static bool LightmapNameLess( const std::string& a, const std::string& b)
{
	return LightmapNameCompare( a.c_str(), b.c_str() ) < 0;
}

void LoadRGBEToFloats( const char *name, float **pic, int *width, int *height )
{
	int      i, j;
	const byte *buf_p;
	float    *floatbuf;
	char     *token;
	int      w, h, c;
	bool formatFound;

	union
	{
		byte  b[ 4 ];
		float f;
	} sample;

	*pic = nullptr;

	// load the file
	std::error_code err;
	std::string buffer = FS::PakPath::ReadFile( name, err );

	if ( err )
	{
		Sys::Drop( "RGBE image '%s' is not found", name );
	}

	buf_p = reinterpret_cast<const byte*>(buffer.c_str());

	formatFound = false;
	w = h = 0;

	while ( true )
	{
		token = COM_ParseExt2( ( const char ** ) &buf_p, true );

		if ( !token[ 0 ] )
		{
			break;
		}

		if ( !Q_stricmp( token, "FORMAT" ) )
		{
			token = COM_ParseExt2( ( const char ** ) &buf_p, false );

			if ( !Q_stricmp( token, "=" ) )
			{
				token = COM_ParseExt2( ( const char ** ) &buf_p, false );

				if ( !Q_stricmp( token, "32" ) )
				{
					token = COM_ParseExt2( ( const char ** ) &buf_p, false );

					if ( !Q_stricmp( token, "-" ) )
					{
						token = COM_ParseExt2( ( const char ** ) &buf_p, false );

						if ( !Q_stricmp( token, "bit_rle_rgbe" ) )
						{
							formatFound = true;
						}
						else
						{
							Log::Warn("RGBE image '%s' has expected 'bit_rle_rgbe' but found '%s' instead",
								name, token );
						}
					}
					else
					{
						Log::Warn("RGBE image '%s' has expected '-' but found '%s' instead", name, token );
					}
				}
				else
				{
					Log::Warn("RGBE image '%s' has expected '32' but found '%s' instead", name, token );
				}
			}
			else
			{
				Log::Warn("RGBE image '%s' has expected '=' but found '%s' instead", name, token );
			}
		}

		if ( !Q_stricmp( token, "-" ) )
		{
			token = COM_ParseExt2( ( const char ** ) &buf_p, false );

			if ( !Q_stricmp( token, "Y" ) )
			{
				token = COM_ParseExt2( ( const char ** ) &buf_p, false );
				w = atoi( token );

				token = COM_ParseExt2( ( const char ** ) &buf_p, false );

				if ( !Q_stricmp( token, "+" ) )
				{
					token = COM_ParseExt2( ( const char ** ) &buf_p, false );

					if ( !Q_stricmp( token, "X" ) )
					{
						token = COM_ParseExt2( ( const char ** ) &buf_p, false );
						h = atoi( token );
						break;
					}
					else
					{
						Log::Warn("RGBE image '%s' has expected 'X' but found '%s' instead", name, token );
					}
				}
				else
				{
					Log::Warn("RGBE image '%s' has expected '+' but found '%s' instead", name, token );
				}
			}
			else
			{
				Log::Warn("RGBE image '%s' has expected 'Y' but found '%s' instead", name, token );
			}
		}
	}

	// go to the first byte
	while ( ( c = *buf_p++ ) != 0 )
	{
		if ( c == '\n' )
		{
			break;
		}
	}

	if ( width )
	{
		*width = w;
	}

	if ( height )
	{
		*height = h;
	}

	if ( !formatFound )
	{
		Sys::Drop( "RGBE image '%s' has no format", name );
	}

	if ( !w || !h )
	{
		Sys::Drop( "RGBE image '%s' has an invalid image size", name );
	}

	*pic = (float*) malloc( w * h * 3 * sizeof( float ) );
	floatbuf = *pic;

	for ( i = 0; i < ( w * h ); i++ )
	{
		for ( j = 0; j < 3; j++ )
		{
			sample.b[ 0 ] = *buf_p++;
			sample.b[ 1 ] = *buf_p++;
			sample.b[ 2 ] = *buf_p++;
			sample.b[ 3 ] = *buf_p++;

			*floatbuf++ = sample.f / 255.0f; // FIXME XMap2's output is 255 times too high
		}
	}
}

static void LoadRGBEToBytes( const char *name, byte **ldrImage, int *width, int *height )
{
	int    i, j;
	int    w, h;
	float  *hdrImage;
	float  *floatbuf;
	byte   *pixbuf;
	vec3_t sample;
	float  max;

	w = h = 0;
	LoadRGBEToFloats( name, &hdrImage, &w, &h );

	*width = w;
	*height = h;

	*ldrImage = (byte*) ri.Z_Malloc( w * h * 4 );
	pixbuf = *ldrImage;

	floatbuf = hdrImage;

	for ( i = 0; i < ( w * h ); i++ )
	{
		for ( j = 0; j < 3; j++ )
		{
			sample[ j ] = *floatbuf++ * 255.0f;
		}

		// clamp with color normalization
		max = sample[ 0 ];

		if ( sample[ 1 ] > max )
		{
			max = sample[ 1 ];
		}

		if ( sample[ 2 ] > max )
		{
			max = sample[ 2 ];
		}

		if ( max > 255.0f )
		{
			VectorScale( sample, ( 255.0f / max ), sample );
		}

		*pixbuf++ = ( byte ) sample[ 0 ];
		*pixbuf++ = ( byte ) sample[ 1 ];
		*pixbuf++ = ( byte ) sample[ 2 ];
		*pixbuf++ = ( byte ) 255;
	}

	free( hdrImage );
}

static std::vector<std::string> R_LoadExternalLightmaps( const char *mapName )
{
	const char *const extensions[] {".png", ".tga", ".webp", ".crn", ".jpg", ".jpeg"};
	std::vector<std::string> files[ ARRAY_LEN( extensions ) ];
	for ( const std::string& filename : FS::PakPath::ListFiles( mapName ) ) {
		for ( int i = 0; i < ARRAY_LEN( extensions ); i++ )
		{
			if ( Str::IsISuffix( extensions[ i ], filename ) )
			{
				files[ i ].push_back( filename );
			}
		}
	}
	for ( auto& fileList : files )
	{
		if ( !fileList.empty() )
		{
			std::sort( fileList.begin(), fileList.end(), LightmapNameLess );
			return std::move( fileList );
		}
	}
	return {};
}

/*
===============
R_LoadLightmaps
===============
*/
static void R_LoadLightmaps( lump_t *l, const char *bspName )
{
	tr.worldLightMapping = r_precomputedLighting->integer && tr.lightMode == lightMode_t::MAP;

	/* All lightmaps will be loaded if either light mapping
	or deluxe mapping is enabled. */
	if ( !tr.worldLightMapping && !tr.worldDeluxeMapping )
	{
		return;
	}

	int len = l->filelen;
	if ( !len )
	{
		char mapName[ MAX_QPATH ];

		Q_strncpyz( mapName, bspName, sizeof( mapName ) );
		COM_StripExtension3( mapName, mapName, sizeof( mapName ) );

		if ( tr.worldHDR_RGBE )
		{
			// we are about to upload textures
			R_SyncRenderThread();

			// load HDR lightmaps
			int  width, height;
			byte *ldrImage;

			std::vector<std::string> hdrFiles;
			for ( const std::string& filename : FS::PakPath::ListFiles( mapName ) )
			{
				if ( Str::IsISuffix( ".hdr", filename ) )
				{
					hdrFiles.push_back( filename );
				}
			}
			if ( hdrFiles.empty() )
			{
				Log::Warn("no lightmap files found");
				tr.worldLightMapping = false;
				tr.worldDeluxeMapping = false;
				return;
			}
			std::sort( hdrFiles.begin(), hdrFiles.end(), LightmapNameLess );

			for ( const std::string& filename : hdrFiles )
			{
				Log::Debug("...loading external lightmap as RGB8 LDR '%s/%s'", mapName, filename );

				width = height = 0;
				LoadRGBEToBytes( va( "%s/%s", mapName, filename.c_str() ), &ldrImage, &width, &height );

				imageParams_t imageParams = {};
				imageParams.bits = IF_NOPICMIP | IF_LIGHTMAP;
				imageParams.filterType = filterType_t::FT_DEFAULT;
				imageParams.wrapType = wrapTypeEnum_t::WT_CLAMP;

				auto image = R_CreateImage( va( "%s/%s", mapName, filename.c_str() ), (const byte **)&ldrImage, width, height, 1, imageParams );

				Com_AddToGrowList( &tr.lightmaps, image );

				ri.Free( ldrImage );
			}

			if (tr.worldDeluxeMapping) {
				// load deluxemaps
				std::vector<std::string> lightmapFiles = R_LoadExternalLightmaps(mapName);
				if (lightmapFiles.empty()) {
					Log::Warn("no lightmap files found");
					tr.worldLightMapping = false;
					tr.worldDeluxeMapping = false;
					return;
				}

				Log::Debug("...loading %i deluxemaps", lightmapFiles.size());

				for (const std::string& filename : lightmapFiles) {
					Log::Debug("...loading external lightmap '%s/%s'", mapName, filename);

					imageParams_t imageParams = {};
					imageParams.bits = IF_NOPICMIP | IF_NORMALMAP;
					imageParams.filterType = filterType_t::FT_DEFAULT;
					imageParams.wrapType = wrapTypeEnum_t::WT_CLAMP;

					auto image = R_FindImageFile(va("%s/%s", mapName, filename.c_str()), imageParams);
					Com_AddToGrowList(&tr.deluxemaps, image);
				}
			}
		}
		else
		{
			std::vector<std::string> lightmapFiles = R_LoadExternalLightmaps(mapName);
			if (lightmapFiles.empty()) {
				Log::Warn("no lightmap files found");
				tr.worldLightMapping = false;
				tr.worldDeluxeMapping = false;
				return;
			}

			Log::Debug("...loading %i lightmaps", lightmapFiles.size() );

			// we are about to upload textures
			R_SyncRenderThread();

			for (size_t i = 0; i < lightmapFiles.size(); i++) {
				Log::Debug("...loading external lightmap '%s/%s'", mapName, lightmapFiles[i]);

				if (!tr.worldDeluxeMapping || i % 2 == 0) {
					imageParams_t imageParams = {};
					imageParams.bits = IF_NOPICMIP | IF_LIGHTMAP;
					imageParams.filterType = filterType_t::FT_LINEAR;
					imageParams.wrapType = wrapTypeEnum_t::WT_CLAMP;

					auto image = R_FindImageFile(va("%s/%s", mapName, lightmapFiles[i].c_str()), imageParams);
					Com_AddToGrowList(&tr.lightmaps, image);
				}
				else if (tr.worldDeluxeMapping)
				{
					imageParams_t imageParams = {};
					imageParams.bits = IF_NOPICMIP | IF_NORMALMAP;
					imageParams.filterType = filterType_t::FT_LINEAR;
					imageParams.wrapType = wrapTypeEnum_t::WT_CLAMP;

					auto image = R_FindImageFile(va("%s/%s", mapName, lightmapFiles[i].c_str()), imageParams);
					Com_AddToGrowList(&tr.deluxemaps, image);
				}
			}
		}
	} else {
		len = l->filelen;

		if ( !len )
		{
			Log::Warn("no lightmap files found");
			tr.worldLightMapping = false;
			tr.worldDeluxeMapping = false;
			return;
		}

		byte *buf = fileBase + l->fileofs;

		// we are about to upload textures
		R_SyncRenderThread();

		const int internalLightMapSize = 128;

		// create all the lightmaps
		int numLightmaps = len / ( internalLightMapSize * internalLightMapSize * 3 );

		if ( numLightmaps == 1 )
		{
			//FIXME: HACK: maps with only one lightmap turn up fullbright for some reason.
			//this hack avoids that scenario, but isn't the correct solution.
			numLightmaps++;
		}
		else if ( numLightmaps >= MAX_LIGHTMAPS )
		{
			// 20051020 misantropia
			Log::Warn("number of lightmaps > MAX_LIGHTMAPS" );
			numLightmaps = MAX_LIGHTMAPS;
		}

		for ( int i = 0; i < numLightmaps; i++ )
		{
			byte *lightMapBuffer = (byte*) ri.Hunk_AllocateTempMemory( sizeof( byte ) * internalLightMapSize * internalLightMapSize * 4 );

			memset( lightMapBuffer, 128, internalLightMapSize * internalLightMapSize * 4 );

			// expand the 24 bit on-disk to 32 bit
			byte *buf_p = buf + i * internalLightMapSize * internalLightMapSize * 3;

			for ( int y = 0; y < internalLightMapSize; y++ )
			{
				for ( int x = 0; x < internalLightMapSize; x++ )
				{
					int index = x + ( y * internalLightMapSize );
					lightMapBuffer[( index * 4 ) + 0 ] = buf_p[( ( x + ( y * internalLightMapSize ) ) * 3 ) + 0 ];
					lightMapBuffer[( index * 4 ) + 1 ] = buf_p[( ( x + ( y * internalLightMapSize ) ) * 3 ) + 1 ];
					lightMapBuffer[( index * 4 ) + 2 ] = buf_p[( ( x + ( y * internalLightMapSize ) ) * 3 ) + 2 ];
					lightMapBuffer[( index * 4 ) + 3 ] = 255;

					R_ColorShiftLightingBytes( &lightMapBuffer[( index * 4 ) + 0 ], &lightMapBuffer[( index * 4 ) + 0 ] );
				}
			}

			imageParams_t imageParams = {};
			imageParams.bits = IF_NOPICMIP | IF_LIGHTMAP;
			imageParams.filterType = filterType_t::FT_DEFAULT;
			imageParams.wrapType = wrapTypeEnum_t::WT_CLAMP;

			image_t *internalLightMap = R_CreateImage( va( "_internalLightMap%d", i ), (const byte **)&lightMapBuffer, internalLightMapSize, internalLightMapSize, 1, imageParams );
			Com_AddToGrowList( &tr.lightmaps, internalLightMap );

			ri.Hunk_FreeTempMemory( lightMapBuffer );
		}
	}
}

/*
=================
RE_SetWorldVisData

This is called by the clipmodel subsystem so we can share the 1.8 megs of
space in big maps...
=================
*/
void RE_SetWorldVisData( const byte *vis )
{
	tr.externalVisData = vis;
}

/*
=================
R_LoadVisibility
=================
*/
static void R_LoadVisibility( lump_t *l )
{
	int  len, i, j, k;
	byte *buf;

	Log::Debug("...loading visibility" );

	len = ( s_worldData.numClusters + 63 ) & ~63;
	s_worldData.novis = (byte*) ri.Hunk_Alloc( len, ha_pref::h_low );
	memset( s_worldData.novis, 0xff, len );

	len = l->filelen;

	if ( !len )
	{
		return;
	}

	buf = fileBase + l->fileofs;

	s_worldData.numClusters = LittleLong( ( ( int * ) buf ) [ 0 ] );
	s_worldData.clusterBytes = LittleLong( ( ( int * ) buf ) [ 1 ] );

	// CM_Load should have given us the vis data to share, so
	// we don't need to allocate another copy
	if ( tr.externalVisData )
	{
		s_worldData.vis = tr.externalVisData;
	}
	else
	{
		byte *dest;

		dest = (byte*) ri.Hunk_Alloc( len - 8, ha_pref::h_low );
		memcpy( dest, buf + 8, len - 8 );
		s_worldData.vis = dest;
	}

	// initialize visvis := vis
	len = s_worldData.numClusters * s_worldData.clusterBytes;
	s_worldData.visvis = (byte*) ri.Hunk_Alloc( len, ha_pref::h_low );
	memcpy( s_worldData.visvis, s_worldData.vis, len );

	for ( i = 0; i < s_worldData.numClusters; i++ )
	{
		const byte *src;
		const int *src2;
		byte *dest;

		src  = s_worldData.vis + i * s_worldData.clusterBytes;
		dest = s_worldData.visvis + i * s_worldData.clusterBytes;

		// for each byte in the current cluster's vis data
		for ( j = 0; j < s_worldData.clusterBytes; j++ )
		{
			byte bitbyte = src[ j ];

			if ( !bitbyte )
			{
				continue;
			}

			for ( k = 0; k < 8; k++ )
			{
				int index;

				// check if this cluster ( k = ( cluster & 7 ) ) is visible from the current cluster
				if ( ! ( bitbyte & ( 1 << k ) ) )
				{
					continue;
				}

				// retrieve vis data for the cluster
				index = ( ( j << 3 ) | k );
				src2 = ( int * ) ( s_worldData.vis + index * s_worldData.clusterBytes );

				// OR this vis data with the current cluster's
				for (unsigned m = 0; m < ( s_worldData.clusterBytes / sizeof( int ) ); m++ )
				{
					( ( int * ) dest )[ m ] |= src2[ m ];
				}
			}
		}
	}
}

//===============================================================================

/*
===============
ShaderForShaderNum
===============
*/
static shader_t *ShaderForShaderNum( int shaderNum )
{
	shader_t  *shader;
	dshader_t *dsh;

	shaderNum = LittleLong( shaderNum ) + 0;  // silence the warning

	if ( shaderNum < 0 || shaderNum >= s_worldData.numShaders )
	{
		Sys::Drop( "ShaderForShaderNum: bad num %i", shaderNum );
	}

	dsh = &s_worldData.shaders[ shaderNum ];

	shader = R_FindShader( dsh->shader, shaderType_t::SHADER_3D_STATIC, RSF_DEFAULT );

	// if the shader had errors, just use default shader
	if ( shader->defaultShader )
	{
		return tr.defaultShader;
	}

	return shader;
}

/*
SphereFromBounds() - ydnar
creates a bounding sphere from a bounding box
*/

static void SphereFromBounds( vec3_t mins, vec3_t maxs, vec3_t origin, float *radius )
{
	vec3_t temp;

	VectorAdd( mins, maxs, origin );
	VectorScale( origin, 0.5, origin );
	VectorSubtract( maxs, origin, temp );
	*radius = VectorLength( temp );
}

/*
FinishGenericSurface() - ydnar
handles final surface classification
*/

static void FinishGenericSurface( dsurface_t *ds, srfGeneric_t *gen, vec3_t pt )
{
	// set bounding sphere
	SphereFromBounds( gen->bounds[ 0 ], gen->bounds[ 1 ], gen->origin, &gen->radius );

	if ( gen->surfaceType == surfaceType_t::SF_FACE )
	{
		srfSurfaceFace_t *srf = ( srfSurfaceFace_t * )gen;
		// take the plane normal from the lightmap vector and classify it
		srf->plane.normal[ 0 ] = LittleFloat( ds->lightmapVecs[ 2 ][ 0 ] );
		srf->plane.normal[ 1 ] = LittleFloat( ds->lightmapVecs[ 2 ][ 1 ] );
		srf->plane.normal[ 2 ] = LittleFloat( ds->lightmapVecs[ 2 ][ 2 ] );
		srf->plane.dist = DotProduct( pt, srf->plane.normal );
		SetPlaneSignbits( &srf->plane );
		srf->plane.type = PlaneTypeForNormal( srf->plane.normal );
	}
}

/*
===============
ParseFace
===============
*/
static void ParseFace( dsurface_t *ds, drawVert_t *verts, bspSurface_t *surf, int *indexes )
{
	int              i, j;
	srfSurfaceFace_t *cv;
	srfTriangle_t    *tri;
	int              numVerts, numTriangles;
	int              realLightmapNum;
	struct vertexComponent_t {
		vec2_t stBounds[ 2 ];
		int    minVertex;
	} *components;
	bool         updated;

	// get lightmap
	realLightmapNum = LittleLong( ds->lightmapNum );

	if ( tr.worldLightMapping || tr.worldDeluxeMapping )
	{
		surf->lightmapNum = realLightmapNum;
	}
	else
	{
		surf->lightmapNum = -1;
	}

	if ( tr.worldDeluxeMapping && surf->lightmapNum >= 2 )
	{
		surf->lightmapNum /= 2;
	}

	// get fog volume
	surf->fogIndex = LittleLong( ds->fogNum ) + 1;

	// get shader value
	surf->shader = ShaderForShaderNum( ds->shaderNum );

	if ( r_singleShader->integer && !surf->shader->isSky )
	{
		surf->shader = tr.defaultShader;
	}

	numVerts = LittleLong( ds->numVerts );

	numTriangles = LittleLong( ds->numIndexes ) / 3;

	cv = (srfSurfaceFace_t*) ri.Hunk_Alloc( sizeof( *cv ), ha_pref::h_low );
	cv->surfaceType = surfaceType_t::SF_FACE;

	cv->numTriangles = numTriangles;
	cv->triangles = (srfTriangle_t*) ri.Hunk_Alloc( numTriangles * sizeof( cv->triangles[ 0 ] ), ha_pref::h_low );

	cv->numVerts = numVerts;
	cv->verts = (srfVert_t*) ri.Hunk_Alloc( numVerts * sizeof( cv->verts[ 0 ] ), ha_pref::h_low );

	surf->data = ( surfaceType_t * ) cv;

	// copy vertexes
	ClearBounds( cv->bounds[ 0 ], cv->bounds[ 1 ] );
	verts += LittleLong( ds->firstVert );

	components = (struct vertexComponent_t *)ri.Hunk_AllocateTempMemory( numVerts * sizeof( struct vertexComponent_t ) );

	for ( i = 0; i < numVerts; i++ )
	{
		for ( j = 0; j < 3; j++ )
		{
			cv->verts[ i ].xyz[ j ] = LittleFloat( verts[ i ].xyz[ j ] );
			cv->verts[ i ].normal[ j ] = LittleFloat( verts[ i ].normal[ j ] );
		}

		AddPointToBounds( cv->verts[ i ].xyz, cv->bounds[ 0 ], cv->bounds[ 1 ] );

		components[ i ].minVertex = i;

		for ( j = 0; j < 2; j++ )
		{
			cv->verts[ i ].st[ j ] = LittleFloat( verts[ i ].st[ j ] );
			cv->verts[ i ].lightmap[ j ] = LittleFloat( verts[ i ].lightmap[ j ] );

			components[ i ].stBounds[ 0 ][ j ] = cv->verts[ i ].st[ j ];
			components[ i ].stBounds[ 1 ][ j ] = cv->verts[ i ].st[ j ];
		}

		cv->verts[ i ].lightmap[ 0 ] = LittleFloat( verts[ i ].lightmap[ 0 ] );
		cv->verts[ i ].lightmap[ 1 ] = LittleFloat( verts[ i ].lightmap[ 1 ] );

		cv->verts[ i ].lightColor = Color::Adapt( verts[ i ].color );


		R_ColorShiftLightingBytes( cv->verts[ i ].lightColor.ToArray(), cv->verts[ i ].lightColor.ToArray() );
	}

	// copy triangles
	indexes += LittleLong( ds->firstIndex );

	for ( i = 0, tri = cv->triangles; i < numTriangles; i++, tri++ )
	{
		for ( j = 0; j < 3; j++ )
		{
			tri->indexes[ j ] = LittleLong( indexes[ i * 3 + j ] );

			if ( tri->indexes[ j ] < 0 || tri->indexes[ j ] >= numVerts )
			{
				Sys::Drop( "Bad index in face surface" );
			}
		}
	}

	// compute strongly connected components and TC bounds per component
	do {
		updated = false;

		for( i = 0, tri = cv->triangles; i < numTriangles; i++, tri++ ) {
			int minVertex = std::min( std::min( components[ tri->indexes[ 0 ] ].minVertex,
						  components[ tri->indexes[ 1 ] ].minVertex ),
					     components[ tri->indexes[ 2 ] ].minVertex );
			for( j = 0; j < 3; j++ ) {
				int vertex = tri->indexes[ j ];
				if( components[ vertex ].minVertex != minVertex ) {
					updated = true;
					components[ vertex ].minVertex = minVertex;
					components[ minVertex ].stBounds[ 0 ][ 0 ] = std::min( components[ minVertex ].stBounds[ 0 ][ 0 ],
											  components[ vertex ].stBounds[ 0 ][ 0 ] );
					components[ minVertex ].stBounds[ 0 ][ 1 ] = std::min( components[ minVertex ].stBounds[ 0 ][ 1 ],
											  components[ vertex ].stBounds[ 0 ][ 1 ] );
					components[ minVertex ].stBounds[ 1 ][ 0 ] = std::max( components[ minVertex ].stBounds[ 1 ][ 0 ],
											  components[ vertex ].stBounds[ 1 ][ 0 ] );
					components[ minVertex ].stBounds[ 1 ][ 1 ] = std::max( components[ minVertex ].stBounds[ 1 ][ 1 ],
											  components[ vertex ].stBounds[ 1 ][ 1 ] );
				}
			}
		}
	} while( updated );

	// center texture coords
	for( i = 0; i < numVerts; i++ ) {
		if( components[ i ].minVertex == i ) {
			for( j = 0; j < 2; j++ ) {
				/* Some words about the “minus 0.5” trick:
				 *
				 * The vertexpack engine tries to optimize texture coords, because fp16 numbers have 1/2048 resolution only between -1.0 and 1.0,
				 * and so we could have several texel rounding errors if the coords are too large. The optimization finds connected surfaces and
				 * then shifts the texture coords so that their average value is near 0. If the range is 0-1, there are two equally good solutions,
				 * either keep the range 0-1 or shift to -1-0, and in this case tiny rounding errors decide which one is used.
				 * For non-animated textures this is no issue, but the rotating textures always rotate around 0.5/0.5, and if the texture coords
				 * are shifted, it shifts the rotation center too. The easy fix is to move the average as close as possible to 0.5 instead of 0.0,
				 * then all standard 0-1 textures will not be shifted.
				 * -- @gimhael https://github.com/DaemonEngine/Daemon/issues/35#issuecomment-507406783
				 *
				 * Instead of round(x - 0.5) we can do floor(x). (using std::floorf)
				 * Surprisingly to me, rintf is apparently a builtin in gcc (so is floorf, but not roundf).
				 * And rounding x - 0.5 actually generates fewer instructions. So it looks like the original version is pretty great.
				 * -- @slipher https://github.com/DaemonEngine/Daemon/pull/208#discussion_r299864660
				 */
				components[ i ].stBounds[ 0 ][ j ] = rintf( 0.5f * (components[ i ].stBounds[ 1 ][ j ] + components[ i ].stBounds[ 0 ][ j ]) - 0.5f );
			}
		}

		for ( j = 0; j < 2; j++ )
		{
			cv->verts[ i ].st[ j ] -= components[ components[ i ].minVertex ].stBounds[ 0 ][ j ];
		}
	}

	ri.Hunk_FreeTempMemory( components );

	// take the plane information from the lightmap vector
	for ( i = 0; i < 3; i++ )
	{
		cv->plane.normal[ i ] = LittleFloat( ds->lightmapVecs[ 2 ][ i ] );
	}

	cv->plane.dist = DotProduct( cv->verts[ 0 ].xyz, cv->plane.normal );
	SetPlaneSignbits( &cv->plane );
	cv->plane.type = PlaneTypeForNormal( cv->plane.normal );

	surf->data = ( surfaceType_t * ) cv;

	{
		srfVert_t *dv0, *dv1, *dv2;
		vec3_t    tangent, binormal;

		for ( i = 0, tri = cv->triangles; i < numTriangles; i++, tri++ )
		{
			dv0 = &cv->verts[ tri->indexes[ 0 ] ];
			dv1 = &cv->verts[ tri->indexes[ 1 ] ];
			dv2 = &cv->verts[ tri->indexes[ 2 ] ];

			R_CalcTangents( tangent, binormal,
					dv0->xyz, dv1->xyz, dv2->xyz,
					dv0->st, dv1->st, dv2->st );
			R_TBNtoQtangents( tangent, binormal, dv0->normal,
					  dv0->qtangent );
			R_TBNtoQtangents( tangent, binormal, dv1->normal,
					  dv1->qtangent );
			R_TBNtoQtangents( tangent, binormal, dv2->normal,
					  dv2->qtangent );
		}
	}

	// finish surface
	FinishGenericSurface( ds, ( srfGeneric_t * ) cv, cv->verts[ 0 ].xyz );
}

/*
===============
ParseMesh
===============
*/
static void ParseMesh( dsurface_t *ds, drawVert_t *verts, bspSurface_t *surf )
{
	srfGridMesh_t        *grid;
	int                  i, j;
	int                  width, height, numPoints;
	static srfVert_t     points[ MAX_PATCH_SIZE * MAX_PATCH_SIZE ];
	vec3_t               bounds[ 2 ];
	vec2_t               stBounds[ 2 ], tcOffset;
	vec3_t               tmpVec;
	static surfaceType_t skipData = surfaceType_t::SF_SKIP;
	int                  realLightmapNum;

	// get lightmap
	realLightmapNum = LittleLong( ds->lightmapNum );

	if ( tr.worldLightMapping || tr.worldDeluxeMapping )
	{
		surf->lightmapNum = realLightmapNum;
	}
	else
	{
		surf->lightmapNum = -1;
	}

	if ( tr.worldDeluxeMapping && surf->lightmapNum >= 2 )
	{
		surf->lightmapNum /= 2;
	}

	// get fog volume
	surf->fogIndex = LittleLong( ds->fogNum ) + 1;

	// get shader value
	surf->shader = ShaderForShaderNum( ds->shaderNum );

	if ( r_singleShader->integer && !surf->shader->isSky )
	{
		surf->shader = tr.defaultShader;
	}

	// we may have a nodraw surface, because they might still need to
	// be around for movement clipping
	if ( s_worldData.shaders[ LittleLong( ds->shaderNum ) ].surfaceFlags & SURF_NODRAW )
	{
		surf->data = &skipData;
		return;
	}

	width = LittleLong( ds->patchWidth );
	height = LittleLong( ds->patchHeight );

	if ( width < 0 || width > MAX_PATCH_SIZE || height < 0 || height > MAX_PATCH_SIZE )
	{
		Sys::Drop( "ParseMesh: bad size" );
	}

	verts += LittleLong( ds->firstVert );
	numPoints = width * height;

	// compute min/max texture coords on the fly
	stBounds[ 0 ][ 0 ] =  99999.0f;
	stBounds[ 0 ][ 1 ] =  99999.0f;
	stBounds[ 1 ][ 0 ] = -99999.0f;
	stBounds[ 1 ][ 1 ] = -99999.0f;

	for ( i = 0; i < numPoints; i++ )
	{
		for ( j = 0; j < 3; j++ )
		{
			points[ i ].xyz[ j ] = LittleFloat( verts[ i ].xyz[ j ] );
			points[ i ].normal[ j ] = LittleFloat( verts[ i ].normal[ j ] );
		}

		for ( j = 0; j < 2; j++ )
		{
			points[ i ].st[ j ] = LittleFloat( verts[ i ].st[ j ] );
			points[ i ].lightmap[ j ] = LittleFloat( verts[ i ].lightmap[ j ] );

			stBounds[ 0 ][ j ] = std::min( stBounds[ 0 ][ j ], points[ i ].st[ j ] );
			stBounds[ 1 ][ j ] = std::max( stBounds[ 1 ][ j ], points[ i ].st[ j ] );
		}

		points[ i ].lightmap[ 0 ] = LittleFloat( verts[ i ].lightmap[ 0 ] );
		points[ i ].lightmap[ 1 ] = LittleFloat( verts[ i ].lightmap[ 1 ] );

		points[ i ].lightColor = Color::Adapt( verts[ i ].color );

		R_ColorShiftLightingBytes( points[ i ].lightColor.ToArray(), points[ i ].lightColor.ToArray() );
	}

	// center texture coords
	for( j = 0; j < 2; j++ ) {
		tcOffset[ j ] = 0.5f * (stBounds[ 1 ][ j ] + stBounds[ 0 ][ j ]);
		tcOffset[ j ] = rintf( tcOffset[ j ] );
	}

	for ( i = 0; i < numPoints; i++ )
	{
		for ( j = 0; j < 2; j++ )
		{
			points[ i ].st[ j ] -= tcOffset[ j ];
		}
	}

	// pre-tesselate
	grid = R_SubdividePatchToGrid( width, height, points );
	surf->data = ( surfaceType_t * ) grid;

	// copy the level of detail origin, which is the center
	// of the group of all curves that must subdivide the same
	// to avoid cracking
	for ( i = 0; i < 3; i++ )
	{
		bounds[ 0 ][ i ] = LittleFloat( ds->lightmapVecs[ 0 ][ i ] );
		bounds[ 1 ][ i ] = LittleFloat( ds->lightmapVecs[ 1 ][ i ] );
	}

	VectorAdd( bounds[ 0 ], bounds[ 1 ], bounds[ 1 ] );
	VectorScale( bounds[ 1 ], 0.5f, grid->lodOrigin );
	VectorSubtract( bounds[ 0 ], grid->lodOrigin, tmpVec );
	grid->lodRadius = VectorLength( tmpVec );

	// finish surface
	FinishGenericSurface( ds, ( srfGeneric_t * ) grid, grid->verts[ 0 ].xyz );
}

/*
===============
ParseTriSurf
===============
*/
static void ParseTriSurf( dsurface_t *ds, drawVert_t *verts, bspSurface_t *surf, int *indexes )
{
	srfTriangles_t       *cv;
	srfTriangle_t        *tri;
	int                  i, j;
	int                  numVerts, numTriangles;
	static surfaceType_t skipData = surfaceType_t::SF_SKIP;
	struct vertexComponent_t {
		vec2_t stBounds[ 2 ];
		int    minVertex;
	} *components;
	bool         updated;

	// get lightmap
	surf->lightmapNum = -1; // FIXME LittleLong(ds->lightmapNum);

	if ( tr.worldDeluxeMapping && surf->lightmapNum >= 2 )
	{
		surf->lightmapNum /= 2;
	}

	// get fog volume
	surf->fogIndex = LittleLong( ds->fogNum ) + 1;

	// get shader
	surf->shader = ShaderForShaderNum( ds->shaderNum );

	if ( r_singleShader->integer && !surf->shader->isSky )
	{
		surf->shader = tr.defaultShader;
	}

	// we may have a nodraw surface, because they might still need to
	// be around for movement clipping
	if ( s_worldData.shaders[ LittleLong( ds->shaderNum ) ].surfaceFlags & SURF_NODRAW )
	{
		surf->data = &skipData;
		return;
	}

	numVerts = LittleLong( ds->numVerts );
	numTriangles = LittleLong( ds->numIndexes ) / 3;

	cv = (srfTriangles_t*) ri.Hunk_Alloc( sizeof( *cv ), ha_pref::h_low );
	cv->surfaceType = surfaceType_t::SF_TRIANGLES;

	cv->numTriangles = numTriangles;
	cv->triangles = (srfTriangle_t*) ri.Hunk_Alloc( numTriangles * sizeof( cv->triangles[ 0 ] ), ha_pref::h_low );

	cv->numVerts = numVerts;
	cv->verts = (srfVert_t*) ri.Hunk_Alloc( numVerts * sizeof( cv->verts[ 0 ] ), ha_pref::h_low );

	surf->data = ( surfaceType_t * ) cv;

	// copy vertexes
	verts += LittleLong( ds->firstVert );

	components = (struct vertexComponent_t *)ri.Hunk_AllocateTempMemory( numVerts * sizeof( struct vertexComponent_t ) );

	for ( i = 0; i < numVerts; i++ )
	{
		components[ i ].minVertex = i;

		for ( j = 0; j < 3; j++ )
		{
			cv->verts[ i ].xyz[ j ] = LittleFloat( verts[ i ].xyz[ j ] );
			cv->verts[ i ].normal[ j ] = LittleFloat( verts[ i ].normal[ j ] );
		}

		for ( j = 0; j < 2; j++ )
		{
			cv->verts[ i ].st[ j ] = LittleFloat( verts[ i ].st[ j ] );
			cv->verts[ i ].lightmap[ j ] = LittleFloat( verts[ i ].lightmap[ j ] );

			components[ i ].stBounds[ 0 ][ j ] = cv->verts[ i ].st[ j ];
			components[ i ].stBounds[ 1 ][ j ] = cv->verts[ i ].st[ j ];
		}

			cv->verts[ i ].lightColor = Color::Adapt( verts[ i ].color );

		R_ColorShiftLightingBytes( cv->verts[ i ].lightColor.ToArray(), cv->verts[ i ].lightColor.ToArray() );
	}

	// copy triangles
	indexes += LittleLong( ds->firstIndex );

	for ( i = 0, tri = cv->triangles; i < numTriangles; i++, tri++ )
	{
		for ( j = 0; j < 3; j++ )
		{
			tri->indexes[ j ] = LittleLong( indexes[ i * 3 + j ] );

			if ( tri->indexes[ j ] < 0 || tri->indexes[ j ] >= numVerts )
			{
				Sys::Drop( "Bad index in face surface" );
			}
		}
	}

	// compute strongly connected components and TC bounds per component
	do {
		updated = false;

		for( i = 0, tri = cv->triangles; i < numTriangles; i++, tri++ ) {
			int minVertex = std::min( std::min( components[ tri->indexes[ 0 ] ].minVertex,
						  components[ tri->indexes[ 1 ] ].minVertex ),
					     components[ tri->indexes[ 2 ] ].minVertex );
			for( j = 0; j < 3; j++ ) {
				int vertex = tri->indexes[ j ];
				if( components[ vertex ].minVertex != minVertex ) {
					updated = true;
					components[ vertex ].minVertex = minVertex;
					components[ minVertex ].stBounds[ 0 ][ 0 ] = std::min( components[ minVertex ].stBounds[ 0 ][ 0 ],
											  components[ vertex ].stBounds[ 0 ][ 0 ] );
					components[ minVertex ].stBounds[ 0 ][ 1 ] = std::min( components[ minVertex ].stBounds[ 0 ][ 1 ],
											  components[ vertex ].stBounds[ 0 ][ 1 ] );
					components[ minVertex ].stBounds[ 1 ][ 0 ] = std::max( components[ minVertex ].stBounds[ 1 ][ 0 ],
											  components[ vertex ].stBounds[ 1 ][ 0 ] );
					components[ minVertex ].stBounds[ 1 ][ 1 ] = std::max( components[ minVertex ].stBounds[ 1 ][ 1 ],
											  components[ vertex ].stBounds[ 1 ][ 1 ] );
				}
			}
		}
	} while( updated );

	// center texture coords
	for( i = 0; i < numVerts; i++ ) {
		if( components[ i ].minVertex == i ) {
			for( j = 0; j < 2; j++ ) {
				/* Reuse the “minus 0.5” trick:
				 *
				 * This is the loader for triangle meshes, there are probably no rotating textures on triangle meshes,
				 * but it's better to keep it consistent.
				 * -- @gimhael https://github.com/DaemonEngine/Daemon/pull/208#discussion_r299809045
				 */
				components[ i ].stBounds[ 0 ][ j ] = rintf( 0.5f * (components[ i ].stBounds[ 1 ][ j ] + components[ i ].stBounds[ 0 ][ j ]) - 0.5f );
			}
		}

		for ( j = 0; j < 2; j++ )
		{
			cv->verts[ i ].st[ j ] -= components[ components[ i ].minVertex ].stBounds[ 0 ][ j ];
		}
	}

	ri.Hunk_FreeTempMemory( components );

	// calc bounding box
	// HACK: don't loop only through the vertices because they can contain bad data with .lwo models ...
	ClearBounds( cv->bounds[ 0 ], cv->bounds[ 1 ] );

	for ( i = 0, tri = cv->triangles; i < numTriangles; i++, tri++ )
	{
		AddPointToBounds( cv->verts[ tri->indexes[ 0 ] ].xyz, cv->bounds[ 0 ], cv->bounds[ 1 ] );
		AddPointToBounds( cv->verts[ tri->indexes[ 1 ] ].xyz, cv->bounds[ 0 ], cv->bounds[ 1 ] );
		AddPointToBounds( cv->verts[ tri->indexes[ 2 ] ].xyz, cv->bounds[ 0 ], cv->bounds[ 1 ] );
	}

	// Tr3B - calc tangent spaces
	{
		srfVert_t *dv0, *dv1, *dv2;
		vec3_t    tangent, binormal;

		for ( i = 0, tri = cv->triangles; i < numTriangles; i++, tri++ )
		{
			dv0 = &cv->verts[ tri->indexes[ 0 ] ];
			dv1 = &cv->verts[ tri->indexes[ 1 ] ];
			dv2 = &cv->verts[ tri->indexes[ 2 ] ];

			R_CalcTangents( tangent, binormal,
					dv0->xyz, dv1->xyz, dv2->xyz,
					dv0->st, dv1->st, dv2->st );
			R_TBNtoQtangents( tangent, binormal, dv0->normal,
					  dv0->qtangent );
			R_TBNtoQtangents( tangent, binormal, dv1->normal,
					  dv1->qtangent );
			R_TBNtoQtangents( tangent, binormal, dv2->normal,
					  dv2->qtangent );
		}
	}

	// finish surface
	FinishGenericSurface( ds, ( srfGeneric_t * ) cv, cv->verts[ 0 ].xyz );
}

/*
===============
ParseFlare
===============
*/
static void ParseFlare( dsurface_t *ds, bspSurface_t *surf )
{
	srfFlare_t *flare;
	int        i;

	// set lightmap
	surf->lightmapNum = -1;

	// get fog volume
	surf->fogIndex = LittleLong( ds->fogNum ) + 1;

	// get shader
	surf->shader = ShaderForShaderNum( ds->shaderNum );

	if ( r_singleShader->integer && !surf->shader->isSky )
	{
		surf->shader = tr.defaultShader;
	}

	flare = (srfFlare_t*) ri.Hunk_Alloc( sizeof( *flare ), ha_pref::h_low );
	flare->surfaceType = surfaceType_t::SF_FLARE;

	surf->data = ( surfaceType_t * ) flare;

	for ( i = 0; i < 3; i++ )
	{
		flare->origin[ i ] = LittleFloat( ds->lightmapOrigin[ i ] );
		flare->color[ i ] = LittleFloat( ds->lightmapVecs[ 0 ][ i ] );
		flare->normal[ i ] = LittleFloat( ds->lightmapVecs[ 2 ][ i ] );
	}
}

/*
=================
R_MergedWidthPoints

returns true if there are grid points merged on a width edge
=================
*/
int R_MergedWidthPoints( srfGridMesh_t *grid, int offset )
{
	int i, j;

	for ( i = 1; i < grid->width - 1; i++ )
	{
		for ( j = i + 1; j < grid->width - 1; j++ )
		{
			if ( fabsf( grid->verts[ i + offset ].xyz[ 0 ] - grid->verts[ j + offset ].xyz[ 0 ] ) > 0.1f )
			{
				continue;
			}

			if ( fabsf( grid->verts[ i + offset ].xyz[ 1 ] - grid->verts[ j + offset ].xyz[ 1 ] ) > 0.1f )
			{
				continue;
			}

			if ( fabsf( grid->verts[ i + offset ].xyz[ 2 ] - grid->verts[ j + offset ].xyz[ 2 ] ) > 0.1f )
			{
				continue;
			}

			return true;
		}
	}

	return false;
}

/*
=================
R_MergedHeightPoints

returns true if there are grid points merged on a height edge
=================
*/
int R_MergedHeightPoints( srfGridMesh_t *grid, int offset )
{
	int i, j;

	for ( i = 1; i < grid->height - 1; i++ )
	{
		for ( j = i + 1; j < grid->height - 1; j++ )
		{
			if ( fabsf( grid->verts[ grid->width * i + offset ].xyz[ 0 ] - grid->verts[ grid->width * j + offset ].xyz[ 0 ] ) > 0.1f )
			{
				continue;
			}

			if ( fabsf( grid->verts[ grid->width * i + offset ].xyz[ 1 ] - grid->verts[ grid->width * j + offset ].xyz[ 1 ] ) > 0.1f )
			{
				continue;
			}

			if ( fabsf( grid->verts[ grid->width * i + offset ].xyz[ 2 ] - grid->verts[ grid->width * j + offset ].xyz[ 2 ] ) > 0.1f )
			{
				continue;
			}

			return true;
		}
	}

	return false;
}

/*
=================
R_FixSharedVertexLodError_r

NOTE: never sync LoD through grid edges with merged points!

FIXME: write generalized version that also avoids cracks between a patch and one that meets half way?
=================
*/
void R_FixSharedVertexLodError_r( int start, srfGridMesh_t *grid1 )
{
	int           j, k, l, m, n, offset1, offset2, touch;
	srfGridMesh_t *grid2;

	for ( j = start; j < s_worldData.numSurfaces; j++ )
	{
		//
		grid2 = ( srfGridMesh_t * ) s_worldData.surfaces[ j ].data;

		// if this surface is not a grid
		if ( grid2->surfaceType != surfaceType_t::SF_GRID )
		{
			continue;
		}

		// if the LOD errors are already fixed for this patch
		if ( grid2->lodFixed == 2 )
		{
			continue;
		}

		// grids in the same LOD group should have the exact same lod radius
		if ( grid1->lodRadius != grid2->lodRadius )
		{
			continue;
		}

		// grids in the same LOD group should have the exact same lod origin
		if ( grid1->lodOrigin[ 0 ] != grid2->lodOrigin[ 0 ] )
		{
			continue;
		}

		if ( grid1->lodOrigin[ 1 ] != grid2->lodOrigin[ 1 ] )
		{
			continue;
		}

		if ( grid1->lodOrigin[ 2 ] != grid2->lodOrigin[ 2 ] )
		{
			continue;
		}

		//
		touch = false;

		for ( n = 0; n < 2; n++ )
		{
			//
			if ( n )
			{
				offset1 = ( grid1->height - 1 ) * grid1->width;
			}
			else
			{
				offset1 = 0;
			}

			if ( R_MergedWidthPoints( grid1, offset1 ) )
			{
				continue;
			}

			for ( k = 1; k < grid1->width - 1; k++ )
			{
				for ( m = 0; m < 2; m++ )
				{
					if ( m )
					{
						offset2 = ( grid2->height - 1 ) * grid2->width;
					}
					else
					{
						offset2 = 0;
					}

					if ( R_MergedWidthPoints( grid2, offset2 ) )
					{
						continue;
					}

					for ( l = 1; l < grid2->width - 1; l++ )
					{
						//
						if ( fabsf( grid1->verts[ k + offset1 ].xyz[ 0 ] - grid2->verts[ l + offset2 ].xyz[ 0 ] ) > 0.1f )
						{
							continue;
						}

						if ( fabsf( grid1->verts[ k + offset1 ].xyz[ 1 ] - grid2->verts[ l + offset2 ].xyz[ 1 ] ) > 0.1f )
						{
							continue;
						}

						if ( fabsf( grid1->verts[ k + offset1 ].xyz[ 2 ] - grid2->verts[ l + offset2 ].xyz[ 2 ] ) > 0.1f )
						{
							continue;
						}

						// ok the points are equal and should have the same lod error
						grid2->widthLodError[ l ] = grid1->widthLodError[ k ];
						touch = true;
					}
				}

				for ( m = 0; m < 2; m++ )
				{
					if ( m )
					{
						offset2 = grid2->width - 1;
					}
					else
					{
						offset2 = 0;
					}

					if ( R_MergedHeightPoints( grid2, offset2 ) )
					{
						continue;
					}

					for ( l = 1; l < grid2->height - 1; l++ )
					{
						//
						if ( fabsf( grid1->verts[ k + offset1 ].xyz[ 0 ] - grid2->verts[ grid2->width * l + offset2 ].xyz[ 0 ] ) > 0.1f )
						{
							continue;
						}

						if ( fabsf( grid1->verts[ k + offset1 ].xyz[ 1 ] - grid2->verts[ grid2->width * l + offset2 ].xyz[ 1 ] ) > 0.1f )
						{
							continue;
						}

						if ( fabsf( grid1->verts[ k + offset1 ].xyz[ 2 ] - grid2->verts[ grid2->width * l + offset2 ].xyz[ 2 ] ) > 0.1f )
						{
							continue;
						}

						// ok the points are equal and should have the same lod error
						grid2->heightLodError[ l ] = grid1->widthLodError[ k ];
						touch = true;
					}
				}
			}
		}

		for ( n = 0; n < 2; n++ )
		{
			//
			if ( n )
			{
				offset1 = grid1->width - 1;
			}
			else
			{
				offset1 = 0;
			}

			if ( R_MergedHeightPoints( grid1, offset1 ) )
			{
				continue;
			}

			for ( k = 1; k < grid1->height - 1; k++ )
			{
				for ( m = 0; m < 2; m++ )
				{
					if ( m )
					{
						offset2 = ( grid2->height - 1 ) * grid2->width;
					}
					else
					{
						offset2 = 0;
					}

					if ( R_MergedWidthPoints( grid2, offset2 ) )
					{
						continue;
					}

					for ( l = 1; l < grid2->width - 1; l++ )
					{
						//
						if ( fabsf( grid1->verts[ grid1->width * k + offset1 ].xyz[ 0 ] - grid2->verts[ l + offset2 ].xyz[ 0 ] ) > 0.1f )
						{
							continue;
						}

						if ( fabsf( grid1->verts[ grid1->width * k + offset1 ].xyz[ 1 ] - grid2->verts[ l + offset2 ].xyz[ 1 ] ) > 0.1f )
						{
							continue;
						}

						if ( fabsf( grid1->verts[ grid1->width * k + offset1 ].xyz[ 2 ] - grid2->verts[ l + offset2 ].xyz[ 2 ] ) > 0.1f )
						{
							continue;
						}

						// ok the points are equal and should have the same lod error
						grid2->widthLodError[ l ] = grid1->heightLodError[ k ];
						touch = true;
					}
				}

				for ( m = 0; m < 2; m++ )
				{
					if ( m )
					{
						offset2 = grid2->width - 1;
					}
					else
					{
						offset2 = 0;
					}

					if ( R_MergedHeightPoints( grid2, offset2 ) )
					{
						continue;
					}

					for ( l = 1; l < grid2->height - 1; l++ )
					{
						//
						if ( fabsf
						     ( grid1->verts[ grid1->width * k + offset1 ].xyz[ 0 ] -
						       grid2->verts[ grid2->width * l + offset2 ].xyz[ 0 ] ) > 0.1f )
						{
							continue;
						}

						if ( fabsf
						     ( grid1->verts[ grid1->width * k + offset1 ].xyz[ 1 ] -
						       grid2->verts[ grid2->width * l + offset2 ].xyz[ 1 ] ) > 0.1f )
						{
							continue;
						}

						if ( fabsf
						     ( grid1->verts[ grid1->width * k + offset1 ].xyz[ 2 ] -
						       grid2->verts[ grid2->width * l + offset2 ].xyz[ 2 ] ) > 0.1f )
						{
							continue;
						}

						// ok the points are equal and should have the same lod error
						grid2->heightLodError[ l ] = grid1->heightLodError[ k ];
						touch = true;
					}
				}
			}
		}

		if ( touch )
		{
			grid2->lodFixed = 2;
			R_FixSharedVertexLodError_r( start, grid2 );
			//NOTE: this would be correct but makes things really slow
		}
	}
}

/*
=================
R_FixSharedVertexLodError

This function assumes that all patches in one group are nicely stitched together for the highest LoD.
If this is not the case this function will still do its job but won't fix the highest LoD cracks.
=================
*/
void R_FixSharedVertexLodError()
{
	int           i;
	srfGridMesh_t *grid1;

	for ( i = 0; i < s_worldData.numSurfaces; i++ )
	{
		//
		grid1 = ( srfGridMesh_t * ) s_worldData.surfaces[ i ].data;

		// if this surface is not a grid
		if ( grid1->surfaceType != surfaceType_t::SF_GRID )
		{
			continue;
		}

		//
		if ( grid1->lodFixed )
		{
			continue;
		}

		//
		grid1->lodFixed = 2;
		// recursively fix other patches in the same LOD group
		R_FixSharedVertexLodError_r( i + 1, grid1 );
	}
}

/*
===============
R_StitchPatches
===============
*/
int R_StitchPatches( int grid1num, int grid2num )
{
	float         *v1, *v2;
	srfGridMesh_t *grid1, *grid2;
	int           k, l, m, n, offset1, offset2, row, column;

	grid1 = ( srfGridMesh_t * ) s_worldData.surfaces[ grid1num ].data;
	grid2 = ( srfGridMesh_t * ) s_worldData.surfaces[ grid2num ].data;

	for ( n = 0; n < 2; n++ )
	{
		//
		if ( n )
		{
			offset1 = ( grid1->height - 1 ) * grid1->width;
		}
		else
		{
			offset1 = 0;
		}

		if ( R_MergedWidthPoints( grid1, offset1 ) )
		{
			continue;
		}

		for ( k = 0; k < grid1->width - 2; k += 2 )
		{
			for ( m = 0; m < 2; m++ )
			{
				if ( grid2->width >= MAX_GRID_SIZE )
				{
					break;
				}

				if ( m )
				{
					offset2 = ( grid2->height - 1 ) * grid2->width;
				}
				else
				{
					offset2 = 0;
				}

				for ( l = 0; l < grid2->width - 1; l++ )
				{
					//
					v1 = grid1->verts[ k + offset1 ].xyz;
					v2 = grid2->verts[ l + offset2 ].xyz;

					if ( fabsf( v1[ 0 ] - v2[ 0 ] ) > 0.1f )
					{
						continue;
					}

					if ( fabsf( v1[ 1 ] - v2[ 1 ] ) > 0.1f )
					{
						continue;
					}

					if ( fabsf( v1[ 2 ] - v2[ 2 ] ) > 0.1f )
					{
						continue;
					}

					v1 = grid1->verts[ k + 2 + offset1 ].xyz;
					v2 = grid2->verts[ l + 1 + offset2 ].xyz;

					if ( fabsf( v1[ 0 ] - v2[ 0 ] ) > 0.1f )
					{
						continue;
					}

					if ( fabsf( v1[ 1 ] - v2[ 1 ] ) > 0.1f )
					{
						continue;
					}

					if ( fabsf( v1[ 2 ] - v2[ 2 ] ) > 0.1f )
					{
						continue;
					}

					//
					v1 = grid2->verts[ l + offset2 ].xyz;
					v2 = grid2->verts[ l + 1 + offset2 ].xyz;

					if ( fabsf( v1[ 0 ] - v2[ 0 ] ) < 0.01f && fabsf( v1[ 1 ] - v2[ 1 ] ) < 0.01f && fabsf( v1[ 2 ] - v2[ 2 ] ) < 0.01f )
					{
						continue;
					}

					//
					// insert column into grid2 right after after column l
					if ( m )
					{
						row = grid2->height - 1;
					}
					else
					{
						row = 0;
					}

					grid2 = R_GridInsertColumn( grid2, l + 1, row, grid1->verts[ k + 1 + offset1 ].xyz, grid1->widthLodError[ k + 1 ] );
					grid2->lodStitched = false;
					s_worldData.surfaces[ grid2num ].data = ( surfaceType_t * ) grid2;
					return true;
				}
			}

			for ( m = 0; m < 2; m++ )
			{
				if ( grid2->height >= MAX_GRID_SIZE )
				{
					break;
				}

				if ( m )
				{
					offset2 = grid2->width - 1;
				}
				else
				{
					offset2 = 0;
				}

				for ( l = 0; l < grid2->height - 1; l++ )
				{
					//
					v1 = grid1->verts[ k + offset1 ].xyz;
					v2 = grid2->verts[ grid2->width * l + offset2 ].xyz;

					if ( fabsf( v1[ 0 ] - v2[ 0 ] ) > 0.1f )
					{
						continue;
					}

					if ( fabsf( v1[ 1 ] - v2[ 1 ] ) > 0.1f )
					{
						continue;
					}

					if ( fabsf( v1[ 2 ] - v2[ 2 ] ) > 0.1f )
					{
						continue;
					}

					v1 = grid1->verts[ k + 2 + offset1 ].xyz;
					v2 = grid2->verts[ grid2->width * ( l + 1 ) + offset2 ].xyz;

					if ( fabsf( v1[ 0 ] - v2[ 0 ] ) > 0.1f )
					{
						continue;
					}

					if ( fabsf( v1[ 1 ] - v2[ 1 ] ) > 0.1f )
					{
						continue;
					}

					if ( fabsf( v1[ 2 ] - v2[ 2 ] ) > 0.1f )
					{
						continue;
					}

					//
					v1 = grid2->verts[ grid2->width * l + offset2 ].xyz;
					v2 = grid2->verts[ grid2->width * ( l + 1 ) + offset2 ].xyz;

					if ( fabsf( v1[ 0 ] - v2[ 0 ] ) < 0.01f && fabsf( v1[ 1 ] - v2[ 1 ] ) < 0.01f && fabsf( v1[ 2 ] - v2[ 2 ] ) < 0.01f )
					{
						continue;
					}

					//
					// insert row into grid2 right after after row l
					if ( m )
					{
						column = grid2->width - 1;
					}
					else
					{
						column = 0;
					}

					grid2 = R_GridInsertRow( grid2, l + 1, column, grid1->verts[ k + 1 + offset1 ].xyz, grid1->widthLodError[ k + 1 ] );
					grid2->lodStitched = false;
					s_worldData.surfaces[ grid2num ].data = ( surfaceType_t * ) grid2;
					return true;
				}
			}
		}
	}

	for ( n = 0; n < 2; n++ )
	{
		//
		if ( n )
		{
			offset1 = grid1->width - 1;
		}
		else
		{
			offset1 = 0;
		}

		if ( R_MergedHeightPoints( grid1, offset1 ) )
		{
			continue;
		}

		for ( k = 0; k < grid1->height - 2; k += 2 )
		{
			for ( m = 0; m < 2; m++ )
			{
				if ( grid2->width >= MAX_GRID_SIZE )
				{
					break;
				}

				if ( m )
				{
					offset2 = ( grid2->height - 1 ) * grid2->width;
				}
				else
				{
					offset2 = 0;
				}

				for ( l = 0; l < grid2->width - 1; l++ )
				{
					//
					v1 = grid1->verts[ grid1->width * k + offset1 ].xyz;
					v2 = grid2->verts[ l + offset2 ].xyz;

					if ( fabsf( v1[ 0 ] - v2[ 0 ] ) > 0.1f )
					{
						continue;
					}

					if ( fabsf( v1[ 1 ] - v2[ 1 ] ) > 0.1f )
					{
						continue;
					}

					if ( fabsf( v1[ 2 ] - v2[ 2 ] ) > 0.1f )
					{
						continue;
					}

					v1 = grid1->verts[ grid1->width * ( k + 2 ) + offset1 ].xyz;
					v2 = grid2->verts[ l + 1 + offset2 ].xyz;

					if ( fabsf( v1[ 0 ] - v2[ 0 ] ) > 0.1f )
					{
						continue;
					}

					if ( fabsf( v1[ 1 ] - v2[ 1 ] ) > 0.1f )
					{
						continue;
					}

					if ( fabsf( v1[ 2 ] - v2[ 2 ] ) > 0.1f )
					{
						continue;
					}

					//
					v1 = grid2->verts[ l + offset2 ].xyz;
					v2 = grid2->verts[( l + 1 ) + offset2 ].xyz;

					if ( fabsf( v1[ 0 ] - v2[ 0 ] ) < 0.01f && fabsf( v1[ 1 ] - v2[ 1 ] ) < 0.01f && fabsf( v1[ 2 ] - v2[ 2 ] ) < 0.01f )
					{
						continue;
					}

					//
					// insert column into grid2 right after after column l
					if ( m )
					{
						row = grid2->height - 1;
					}
					else
					{
						row = 0;
					}

					grid2 = R_GridInsertColumn( grid2, l + 1, row,
					                            grid1->verts[ grid1->width * ( k + 1 ) + offset1 ].xyz, grid1->heightLodError[ k + 1 ] );
					grid2->lodStitched = false;
					s_worldData.surfaces[ grid2num ].data = ( surfaceType_t * ) grid2;
					return true;
				}
			}

			for ( m = 0; m < 2; m++ )
			{
				if ( grid2->height >= MAX_GRID_SIZE )
				{
					break;
				}

				if ( m )
				{
					offset2 = grid2->width - 1;
				}
				else
				{
					offset2 = 0;
				}

				for ( l = 0; l < grid2->height - 1; l++ )
				{
					//
					v1 = grid1->verts[ grid1->width * k + offset1 ].xyz;
					v2 = grid2->verts[ grid2->width * l + offset2 ].xyz;

					if ( fabsf( v1[ 0 ] - v2[ 0 ] ) > 0.1f )
					{
						continue;
					}

					if ( fabsf( v1[ 1 ] - v2[ 1 ] ) > 0.1f )
					{
						continue;
					}

					if ( fabsf( v1[ 2 ] - v2[ 2 ] ) > 0.1f )
					{
						continue;
					}

					v1 = grid1->verts[ grid1->width * ( k + 2 ) + offset1 ].xyz;
					v2 = grid2->verts[ grid2->width * ( l + 1 ) + offset2 ].xyz;

					if ( fabsf( v1[ 0 ] - v2[ 0 ] ) > 0.1f )
					{
						continue;
					}

					if ( fabsf( v1[ 1 ] - v2[ 1 ] ) > 0.1f )
					{
						continue;
					}

					if ( fabsf( v1[ 2 ] - v2[ 2 ] ) > 0.1f )
					{
						continue;
					}

					//
					v1 = grid2->verts[ grid2->width * l + offset2 ].xyz;
					v2 = grid2->verts[ grid2->width * ( l + 1 ) + offset2 ].xyz;

					if ( fabsf( v1[ 0 ] - v2[ 0 ] ) < 0.01f && fabsf( v1[ 1 ] - v2[ 1 ] ) < 0.01f && fabsf( v1[ 2 ] - v2[ 2 ] ) < 0.01f )
					{
						continue;
					}

					//
					// insert row into grid2 right after after row l
					if ( m )
					{
						column = grid2->width - 1;
					}
					else
					{
						column = 0;
					}

					grid2 = R_GridInsertRow( grid2, l + 1, column,
					                         grid1->verts[ grid1->width * ( k + 1 ) + offset1 ].xyz, grid1->heightLodError[ k + 1 ] );
					grid2->lodStitched = false;
					s_worldData.surfaces[ grid2num ].data = ( surfaceType_t * ) grid2;
					return true;
				}
			}
		}
	}

	for ( n = 0; n < 2; n++ )
	{
		//
		if ( n )
		{
			offset1 = ( grid1->height - 1 ) * grid1->width;
		}
		else
		{
			offset1 = 0;
		}

		if ( R_MergedWidthPoints( grid1, offset1 ) )
		{
			continue;
		}

		for ( k = grid1->width - 1; k > 1; k -= 2 )
		{
			for ( m = 0; m < 2; m++ )
			{
				if ( grid2->width >= MAX_GRID_SIZE )
				{
					break;
				}

				if ( m )
				{
					offset2 = ( grid2->height - 1 ) * grid2->width;
				}
				else
				{
					offset2 = 0;
				}

				for ( l = 0; l < grid2->width - 1; l++ )
				{
					//
					v1 = grid1->verts[ k + offset1 ].xyz;
					v2 = grid2->verts[ l + offset2 ].xyz;

					if ( fabsf( v1[ 0 ] - v2[ 0 ] ) > 0.1f )
					{
						continue;
					}

					if ( fabsf( v1[ 1 ] - v2[ 1 ] ) > 0.1f )
					{
						continue;
					}

					if ( fabsf( v1[ 2 ] - v2[ 2 ] ) > 0.1f )
					{
						continue;
					}

					v1 = grid1->verts[ k - 2 + offset1 ].xyz;
					v2 = grid2->verts[ l + 1 + offset2 ].xyz;

					if ( fabsf( v1[ 0 ] - v2[ 0 ] ) > 0.1f )
					{
						continue;
					}

					if ( fabsf( v1[ 1 ] - v2[ 1 ] ) > 0.1f )
					{
						continue;
					}

					if ( fabsf( v1[ 2 ] - v2[ 2 ] ) > 0.1f )
					{
						continue;
					}

					//
					v1 = grid2->verts[ l + offset2 ].xyz;
					v2 = grid2->verts[( l + 1 ) + offset2 ].xyz;

					if ( fabsf( v1[ 0 ] - v2[ 0 ] ) < 0.01f && fabsf( v1[ 1 ] - v2[ 1 ] ) < 0.01f && fabsf( v1[ 2 ] - v2[ 2 ] ) < 0.01f )
					{
						continue;
					}

					//
					// insert column into grid2 right after after column l
					if ( m )
					{
						row = grid2->height - 1;
					}
					else
					{
						row = 0;
					}

					grid2 = R_GridInsertColumn( grid2, l + 1, row, grid1->verts[ k - 1 + offset1 ].xyz, grid1->widthLodError[ k - 1 ] );
					grid2->lodStitched = false;
					s_worldData.surfaces[ grid2num ].data = ( surfaceType_t * ) grid2;
					return true;
				}
			}

			for ( m = 0; m < 2; m++ )
			{
				if ( grid2->height >= MAX_GRID_SIZE )
				{
					break;
				}

				if ( m )
				{
					offset2 = grid2->width - 1;
				}
				else
				{
					offset2 = 0;
				}

				for ( l = 0; l < grid2->height - 1; l++ )
				{
					//
					v1 = grid1->verts[ k + offset1 ].xyz;
					v2 = grid2->verts[ grid2->width * l + offset2 ].xyz;

					if ( fabsf( v1[ 0 ] - v2[ 0 ] ) > 0.1f )
					{
						continue;
					}

					if ( fabsf( v1[ 1 ] - v2[ 1 ] ) > 0.1f )
					{
						continue;
					}

					if ( fabsf( v1[ 2 ] - v2[ 2 ] ) > 0.1f )
					{
						continue;
					}

					v1 = grid1->verts[ k - 2 + offset1 ].xyz;
					v2 = grid2->verts[ grid2->width * ( l + 1 ) + offset2 ].xyz;

					if ( fabsf( v1[ 0 ] - v2[ 0 ] ) > 0.1f )
					{
						continue;
					}

					if ( fabsf( v1[ 1 ] - v2[ 1 ] ) > 0.1f )
					{
						continue;
					}

					if ( fabsf( v1[ 2 ] - v2[ 2 ] ) > 0.1f )
					{
						continue;
					}

					//
					v1 = grid2->verts[ grid2->width * l + offset2 ].xyz;
					v2 = grid2->verts[ grid2->width * ( l + 1 ) + offset2 ].xyz;

					if ( fabsf( v1[ 0 ] - v2[ 0 ] ) < 0.01f && fabsf( v1[ 1 ] - v2[ 1 ] ) < 0.01f && fabsf( v1[ 2 ] - v2[ 2 ] ) < 0.01f )
					{
						continue;
					}

					//
					// insert row into grid2 right after after row l
					if ( m )
					{
						column = grid2->width - 1;
					}
					else
					{
						column = 0;
					}

					grid2 = R_GridInsertRow( grid2, l + 1, column, grid1->verts[ k - 1 + offset1 ].xyz, grid1->widthLodError[ k - 1 ] );

					if ( !grid2 )
					{
						break;
					}

					grid2->lodStitched = false;
					s_worldData.surfaces[ grid2num ].data = ( surfaceType_t * ) grid2;
					return true;
				}
			}
		}
	}

	for ( n = 0; n < 2; n++ )
	{
		//
		if ( n )
		{
			offset1 = grid1->width - 1;
		}
		else
		{
			offset1 = 0;
		}

		if ( R_MergedHeightPoints( grid1, offset1 ) )
		{
			continue;
		}

		for ( k = grid1->height - 1; k > 1; k -= 2 )
		{
			for ( m = 0; m < 2; m++ )
			{
				if ( grid2->width >= MAX_GRID_SIZE )
				{
					break;
				}

				if ( m )
				{
					offset2 = ( grid2->height - 1 ) * grid2->width;
				}
				else
				{
					offset2 = 0;
				}

				for ( l = 0; l < grid2->width - 1; l++ )
				{
					//
					v1 = grid1->verts[ grid1->width * k + offset1 ].xyz;
					v2 = grid2->verts[ l + offset2 ].xyz;

					if ( fabsf( v1[ 0 ] - v2[ 0 ] ) > 0.1f )
					{
						continue;
					}

					if ( fabsf( v1[ 1 ] - v2[ 1 ] ) > 0.1f )
					{
						continue;
					}

					if ( fabsf( v1[ 2 ] - v2[ 2 ] ) > 0.1f )
					{
						continue;
					}

					v1 = grid1->verts[ grid1->width * ( k - 2 ) + offset1 ].xyz;
					v2 = grid2->verts[ l + 1 + offset2 ].xyz;

					if ( fabsf( v1[ 0 ] - v2[ 0 ] ) > 0.1f )
					{
						continue;
					}

					if ( fabsf( v1[ 1 ] - v2[ 1 ] ) > 0.1f )
					{
						continue;
					}

					if ( fabsf( v1[ 2 ] - v2[ 2 ] ) > 0.1f )
					{
						continue;
					}

					//
					v1 = grid2->verts[ l + offset2 ].xyz;
					v2 = grid2->verts[( l + 1 ) + offset2 ].xyz;

					if ( fabsf( v1[ 0 ] - v2[ 0 ] ) < 0.01f && fabsf( v1[ 1 ] - v2[ 1 ] ) < 0.01f && fabsf( v1[ 2 ] - v2[ 2 ] ) < 0.01f )
					{
						continue;
					}

					//
					// insert column into grid2 right after after column l
					if ( m )
					{
						row = grid2->height - 1;
					}
					else
					{
						row = 0;
					}

					grid2 = R_GridInsertColumn( grid2, l + 1, row,
					                            grid1->verts[ grid1->width * ( k - 1 ) + offset1 ].xyz, grid1->heightLodError[ k - 1 ] );
					grid2->lodStitched = false;
					s_worldData.surfaces[ grid2num ].data = ( surfaceType_t * ) grid2;
					return true;
				}
			}

			for ( m = 0; m < 2; m++ )
			{
				if ( grid2->height >= MAX_GRID_SIZE )
				{
					break;
				}

				if ( m )
				{
					offset2 = grid2->width - 1;
				}
				else
				{
					offset2 = 0;
				}

				for ( l = 0; l < grid2->height - 1; l++ )
				{
					//
					v1 = grid1->verts[ grid1->width * k + offset1 ].xyz;
					v2 = grid2->verts[ grid2->width * l + offset2 ].xyz;

					if ( fabsf( v1[ 0 ] - v2[ 0 ] ) > 0.1f )
					{
						continue;
					}

					if ( fabsf( v1[ 1 ] - v2[ 1 ] ) > 0.1f )
					{
						continue;
					}

					if ( fabsf( v1[ 2 ] - v2[ 2 ] ) > 0.1f )
					{
						continue;
					}

					v1 = grid1->verts[ grid1->width * ( k - 2 ) + offset1 ].xyz;
					v2 = grid2->verts[ grid2->width * ( l + 1 ) + offset2 ].xyz;

					if ( fabsf( v1[ 0 ] - v2[ 0 ] ) > 0.1f )
					{
						continue;
					}

					if ( fabsf( v1[ 1 ] - v2[ 1 ] ) > 0.1f )
					{
						continue;
					}

					if ( fabsf( v1[ 2 ] - v2[ 2 ] ) > 0.1f )
					{
						continue;
					}

					//
					v1 = grid2->verts[ grid2->width * l + offset2 ].xyz;
					v2 = grid2->verts[ grid2->width * ( l + 1 ) + offset2 ].xyz;

					if ( fabsf( v1[ 0 ] - v2[ 0 ] ) < 0.01f && fabsf( v1[ 1 ] - v2[ 1 ] ) < 0.01f && fabsf( v1[ 2 ] - v2[ 2 ] ) < 0.01f )
					{
						continue;
					}

					//
					// insert row into grid2 right after after row l
					if ( m )
					{
						column = grid2->width - 1;
					}
					else
					{
						column = 0;
					}

					grid2 = R_GridInsertRow( grid2, l + 1, column,
					                         grid1->verts[ grid1->width * ( k - 1 ) + offset1 ].xyz, grid1->heightLodError[ k - 1 ] );
					grid2->lodStitched = false;
					s_worldData.surfaces[ grid2num ].data = ( surfaceType_t * ) grid2;
					return true;
				}
			}
		}
	}

	return false;
}

/*
===============
R_TryStitchingPatch

This function will try to stitch patches in the same LoD group together for the highest LoD.

Only single missing vertice cracks will be fixed.

Vertices will be joined at the patch side a crack is first found, at the other side
of the patch (on the same row or column) the vertices will not be joined and cracks
might still appear at that side.
===============
*/
int R_TryStitchingPatch( int grid1num )
{
	int           j, numstitches;
	srfGridMesh_t *grid1, *grid2;

	numstitches = 0;
	grid1 = ( srfGridMesh_t * ) s_worldData.surfaces[ grid1num ].data;

	for ( j = 0; j < s_worldData.numSurfaces; j++ )
	{
		//
		grid2 = ( srfGridMesh_t * ) s_worldData.surfaces[ j ].data;

		// if this surface is not a grid
		if ( grid2->surfaceType != surfaceType_t::SF_GRID )
		{
			continue;
		}

		// grids in the same LOD group should have the exact same lod radius
		if ( grid1->lodRadius != grid2->lodRadius )
		{
			continue;
		}

		// grids in the same LOD group should have the exact same lod origin
		if ( grid1->lodOrigin[ 0 ] != grid2->lodOrigin[ 0 ] )
		{
			continue;
		}

		if ( grid1->lodOrigin[ 1 ] != grid2->lodOrigin[ 1 ] )
		{
			continue;
		}

		if ( grid1->lodOrigin[ 2 ] != grid2->lodOrigin[ 2 ] )
		{
			continue;
		}

		//
		while ( R_StitchPatches( grid1num, j ) )
		{
			numstitches++;
		}
	}

	return numstitches;
}

/*
===============
R_StitchAllPatches
===============
*/
void R_StitchAllPatches()
{
	int           i, stitched, numstitches;
	srfGridMesh_t *grid1;

	Log::Debug("...stitching LoD cracks" );

	numstitches = 0;

	do
	{
		stitched = false;

		for ( i = 0; i < s_worldData.numSurfaces; i++ )
		{
			//
			grid1 = ( srfGridMesh_t * ) s_worldData.surfaces[ i ].data;

			// if this surface is not a grid
			if ( grid1->surfaceType != surfaceType_t::SF_GRID )
			{
				continue;
			}

			//
			if ( grid1->lodStitched )
			{
				continue;
			}

			//
			grid1->lodStitched = true;
			stitched = true;
			//
			numstitches += R_TryStitchingPatch( i );
		}
	}
	while ( stitched );

	Log::Debug("stitched %d LoD cracks", numstitches );
}

/*
===============
R_MovePatchSurfacesToHunk
===============
*/
void R_MovePatchSurfacesToHunk()
{
	int           i, size;
	srfGridMesh_t *grid, *hunkgrid;

	for ( i = 0; i < s_worldData.numSurfaces; i++ )
	{
		//
		grid = ( srfGridMesh_t * ) s_worldData.surfaces[ i ].data;

		// if this surface is not a grid
		if ( grid->surfaceType != surfaceType_t::SF_GRID )
		{
			continue;
		}

		//
		size = sizeof( *grid );
		hunkgrid = (srfGridMesh_t*) ri.Hunk_Alloc( size, ha_pref::h_low );
		memcpy( hunkgrid, grid, size );

		hunkgrid->widthLodError = (float*) ri.Hunk_Alloc( grid->width * 4, ha_pref::h_low );
		memcpy( hunkgrid->widthLodError, grid->widthLodError, grid->width * 4 );

		hunkgrid->heightLodError = (float*) ri.Hunk_Alloc( grid->height * 4, ha_pref::h_low );
		memcpy( hunkgrid->heightLodError, grid->heightLodError, grid->height * 4 );

		hunkgrid->numTriangles = grid->numTriangles;
		hunkgrid->triangles = (srfTriangle_t*) ri.Hunk_Alloc( grid->numTriangles * sizeof( srfTriangle_t ), ha_pref::h_low );
		memcpy( hunkgrid->triangles, grid->triangles, grid->numTriangles * sizeof( srfTriangle_t ) );

		hunkgrid->numVerts = grid->numVerts;
		hunkgrid->verts = (srfVert_t*) ri.Hunk_Alloc( grid->numVerts * sizeof( srfVert_t ), ha_pref::h_low );
		memcpy( hunkgrid->verts, grid->verts, grid->numVerts * sizeof( srfVert_t ) );

		R_FreeSurfaceGridMesh( grid );

		s_worldData.surfaces[ i ].data = ( surfaceType_t * ) hunkgrid;
	}
}

static void CopyVert( const srfVert_t *in, srfVert_t *out )
{
	int j;

	for ( j = 0; j < 3; j++ )
	{
		out->xyz[ j ] = in->xyz[ j ];
		out->normal[ j ] = in->normal[ j ];
	}

	for ( j = 0; j < 2; j++ )
	{
		out->st[ j ] = in->st[ j ];
		out->lightmap[ j ] = in->lightmap[ j ];
	}

	out->lightColor = in->lightColor;

	for ( j = 0; j < 4; j++ )
	{
		out->qtangent[ j ] = in->qtangent[ j ];
	}
}

/*
=================
R_CreateClusters
=================
*/
static void R_CreateClusters()
{
	int          i, j;
	bspSurface_t *surface;
	bspNode_t    *node;

	// reset surfaces' viewCount
	for ( i = 0, surface = s_worldData.surfaces; i < s_worldData.numSurfaces; i++, surface++ )
	{
		surface->viewCount = -1;
	}

	for ( j = 0, node = s_worldData.nodes; j < s_worldData.numnodes; j++, node++ )
	{
		node->visCounts[ 0 ] = -1;
	}
}

/*
SmoothNormals()
smooths together coincident vertex normals across the bsp
*/
static int LeafSurfaceCompare( const void *a, const void *b )
{
	bspSurface_t *aa, *bb;

	aa = * ( bspSurface_t ** ) a;
	bb = * ( bspSurface_t ** ) b;

	// shader first
	if ( aa->shader < bb->shader )
	{
		return -1;
	}

	else if ( aa->shader > bb->shader )
	{
		return 1;
	}

	// by lightmap
	if ( aa->lightmapNum < bb->lightmapNum )
	{
		return -1;
	}

	else if ( aa->lightmapNum > bb->lightmapNum )
	{
		return 1;
	}

	if ( aa->fogIndex < bb->fogIndex )
	{
		return -1;
	}
	else if ( aa->fogIndex > bb->fogIndex )
	{
		return 1;
	}

	// sort by leaf
	if ( aa->interactionBits < bb->interactionBits )
	{
		return -1;
	}
	else if ( aa->interactionBits > bb->interactionBits )
	{
		return 1;
	}

	// sort by leaf marksurfaces index to increase the likelihood of multidraw merging in the backend
	if ( aa->lightCount < bb->lightCount )
	{
		return -1;
	}
	else if ( aa->lightCount > bb->lightCount )
	{
		return 1;
	}
	return 0;
}

/*
===============
R_CreateWorldVBO
===============
*/
static void R_CreateWorldVBO()
{
	int       i, j, k;

	int       numVerts;
	srfVert_t *verts;
	shaderVertex_t *vboVerts;
	glIndex_t      *vboIdxs;

	int           numTriangles;
	srfTriangle_t *triangles;

	int            numSurfaces;
	bspSurface_t  *surface;
	bspSurface_t  **surfaces;
	bspSurface_t  *mergedSurf;
	int           startTime, endTime;

	startTime = ri.Milliseconds();

	numVerts = 0;
	numTriangles = 0;
	numSurfaces = 0;

	for ( k = 0; k < s_worldData.numSurfaces; k++ )
	{
		surface = &s_worldData.surfaces[ k ];

		if ( surface->shader->isSky || surface->shader->isPortal )
		{
			continue;
		}

		if ( *surface->data == surfaceType_t::SF_FACE )
		{
			srfSurfaceFace_t *face = ( srfSurfaceFace_t * ) surface->data;

			numVerts += face->numVerts;
			numTriangles += face->numTriangles;
		}
		else if ( *surface->data == surfaceType_t::SF_GRID )
		{
			srfGridMesh_t *grid = ( srfGridMesh_t * ) surface->data;

			numVerts += grid->numVerts;
			numTriangles += grid->numTriangles;
		}
		else if ( *surface->data == surfaceType_t::SF_TRIANGLES )
		{
			srfTriangles_t *tri = ( srfTriangles_t * ) surface->data;

			numVerts += tri->numVerts;
			numTriangles += tri->numTriangles;
		}
		else
		{
			continue;
		}

		numSurfaces++;
	}

	if ( !numVerts || !numTriangles || !numSurfaces )
	{
		return;
	}

	// reset surface view counts
	for ( i = 0; i < s_worldData.numSurfaces; i++ )
	{
		surface = &s_worldData.surfaces[ i ];

		surface->viewCount = -1;
		surface->lightCount = -1;
		surface->interactionBits = 0;
	}

	// mark matching surfaces
	for ( i = 0; i < s_worldData.numnodes - s_worldData.numDecisionNodes; i++ )
	{
		bspNode_t *leaf = s_worldData.nodes + s_worldData.numDecisionNodes + i;

		for ( j = 0; j < leaf->numMarkSurfaces; j++ )
		{
			bspSurface_t *surf1;
			shader_t *shader1;
			int fogIndex1;
			int lightMapNum1;
			bool merged = false;
			surf1 = s_worldData.markSurfaces[ leaf->firstMarkSurface + j ];

			if ( surf1->viewCount != -1 )
			{
				continue;
			}

			if ( *surf1->data != surfaceType_t::SF_GRID && *surf1->data != surfaceType_t::SF_TRIANGLES && *surf1->data != surfaceType_t::SF_FACE )
			{
				continue;
			}

			shader1 = surf1->shader;

			if ( shader1->isSky || shader1->isPortal )
			{
				continue;
			}

			fogIndex1 = surf1->fogIndex;
			lightMapNum1 = surf1->lightmapNum;
			surf1->viewCount = surf1 - s_worldData.surfaces;
			surf1->lightCount = j;
			surf1->interactionBits = i;

			for ( k = j + 1; k < leaf->numMarkSurfaces; k++ )
			{
				bspSurface_t *surf2;
				shader_t *shader2;
				int fogIndex2;
				int lightMapNum2;

				surf2 = s_worldData.markSurfaces[ leaf->firstMarkSurface + k ];

				if ( surf2->viewCount != -1 )
				{
					continue;
				}

				if ( *surf2->data != surfaceType_t::SF_GRID && *surf2->data != surfaceType_t::SF_TRIANGLES && *surf2->data != surfaceType_t::SF_FACE )
				{
					continue;
				}

				shader2 = surf2->shader;
				fogIndex2 = surf2->fogIndex;
				lightMapNum2 = surf2->lightmapNum;
				if ( shader1 != shader2 || fogIndex1 != fogIndex2 || lightMapNum1 != lightMapNum2 )
				{
					continue;
				}

				surf2->viewCount = surf1->viewCount;
				surf2->lightCount = k;
				surf2->interactionBits = i;
				merged = true;
			}

			if ( !merged )
			{
				surf1->viewCount = -1;
				surf1->lightCount = -1;
				// don't clear the leaf number so
				// surfaces that arn't merged are placed
				// closer to other leafs in the vbo
			}
		}
	}

	surfaces = ( bspSurface_t ** ) ri.Hunk_AllocateTempMemory( sizeof( *surfaces ) * numSurfaces );

	numSurfaces = 0;
	for ( k = 0; k < s_worldData.numSurfaces; k++ )
	{
		surface = &s_worldData.surfaces[ k ];

		if ( surface->shader->isSky || surface->shader->isPortal )
		{
			continue;
		}

		if ( *surface->data == surfaceType_t::SF_FACE || *surface->data == surfaceType_t::SF_GRID || *surface->data == surfaceType_t::SF_TRIANGLES )
		{
			surfaces[ numSurfaces++ ] = surface;
		}
	}

	qsort( surfaces, numSurfaces, sizeof( *surfaces ), LeafSurfaceCompare );

	Log::Debug("...calculating world VBO ( %i verts %i tris )", numVerts, numTriangles );

	// create arrays
	s_worldData.numVerts = numVerts;
	s_worldData.verts = verts = (srfVert_t*) ri.Hunk_Alloc( numVerts * sizeof( srfVert_t ), ha_pref::h_low );

	s_worldData.numTriangles = numTriangles;
	s_worldData.triangles = triangles = (srfTriangle_t*) ri.Hunk_Alloc( numTriangles * sizeof( srfTriangle_t ), ha_pref::h_low );

	vboVerts = (shaderVertex_t *)ri.Hunk_AllocateTempMemory( numVerts * sizeof( shaderVertex_t ) );
	vboIdxs = (glIndex_t *)ri.Hunk_AllocateTempMemory( 3 * numTriangles * sizeof( glIndex_t ) );

	// set up triangle and vertex arrays
	numVerts = 0;
	numTriangles = 0;
	tess.buildingVBO = true; // no batch splitting please
	tess.verts = vboVerts;
	tess.numVertexes = 0;
	tess.indexes = vboIdxs;
	tess.numIndexes = 0;

	for ( k = 0; k < numSurfaces; k++ )
	{
		surface = surfaces[ k ];

		if ( *surface->data == surfaceType_t::SF_FACE )
		{
			srfSurfaceFace_t *srf = ( srfSurfaceFace_t * ) surface->data;

			srf->firstTriangle = numTriangles;
			srf->firstVert = numVerts;

			if ( srf->numTriangles )
			{
				srfTriangle_t *tri;

				for ( i = 0, tri = srf->triangles; i < srf->numTriangles; i++, tri++ )
				{
					for ( j = 0; j < 3; j++ )
					{
						triangles[ numTriangles + i ].indexes[ j ] = srf->firstVert + tri->indexes[ j ];
					}
				}

				numTriangles += srf->numTriangles;
			}

			if ( srf->numVerts )
			{
				for ( i = 0; i < srf->numVerts; i++ )
				{
					CopyVert( &srf->verts[ i ], &verts[ srf->firstVert + i ] );
				}

				numVerts += srf->numVerts;
			}

			rb_surfaceTable[Util::ordinal(surfaceType_t::SF_FACE)](srf );
			Tess_AutospriteDeform( surface->shader->autoSpriteMode, srf->firstVert, srf->numVerts,
					       3 * srf->firstTriangle, 3 * srf->numTriangles );
		}
		else if ( *surface->data == surfaceType_t::SF_GRID )
		{
			srfGridMesh_t *srf = ( srfGridMesh_t * ) surface->data;

			srf->firstTriangle = numTriangles;
			srf->firstVert = numVerts;

			if ( srf->numTriangles )
			{
				srfTriangle_t *tri;

				for ( i = 0, tri = srf->triangles; i < srf->numTriangles; i++, tri++ )
				{
					for ( j = 0; j < 3; j++ )
					{
						triangles[ numTriangles + i ].indexes[ j ] = srf->firstVert + tri->indexes[ j ];
					}
				}

				numTriangles += srf->numTriangles;
			}

			if ( srf->numVerts )
			{
				for ( i = 0; i < srf->numVerts; i++ )
				{
					CopyVert( &srf->verts[ i ], &verts[ srf->firstVert + i ] );
				}

				numVerts += srf->numVerts;
			}

			rb_surfaceTable[Util::ordinal(surfaceType_t::SF_GRID)](srf );
			Tess_AutospriteDeform( surface->shader->autoSpriteMode, srf->firstVert, srf->numVerts,
					       3 * srf->firstTriangle, 3 * srf->numTriangles );
		}
		else if ( *surface->data == surfaceType_t::SF_TRIANGLES )
		{
			srfTriangles_t *srf = ( srfTriangles_t * ) surface->data;

			srf->firstTriangle = numTriangles;
			srf->firstVert = numVerts;

			if ( srf->numTriangles )
			{
				srfTriangle_t *tri;

				for ( i = 0, tri = srf->triangles; i < srf->numTriangles; i++, tri++ )
				{
					for ( j = 0; j < 3; j++ )
					{
						triangles[ numTriangles + i ].indexes[ j ] = srf->firstVert + tri->indexes[ j ];
					}
				}

				numTriangles += srf->numTriangles;
			}

			if ( srf->numVerts )
			{
				for ( i = 0; i < srf->numVerts; i++ )
				{
					CopyVert( &srf->verts[ i ], &verts[ srf->firstVert + i ] );
				}

				numVerts += srf->numVerts;
			}

			rb_surfaceTable[Util::ordinal(surfaceType_t::SF_TRIANGLES)](srf );
			Tess_AutospriteDeform( surface->shader->autoSpriteMode, srf->firstVert, srf->numVerts,
					       3 * srf->firstTriangle, 3 * srf->numTriangles );
		}
	}

	s_worldData.vbo = R_CreateStaticVBO2( va( "staticWorld_VBO %i", 0 ), numVerts, vboVerts,
					      ATTR_POSITION | ATTR_TEXCOORD | ATTR_QTANGENT | ATTR_COLOR
	                                 );
	s_worldData.ibo = R_CreateStaticIBO2( va( "staticWorld_IBO %i", 0 ), numTriangles, vboIdxs );

	tess.numVertexes = 0;
	tess.numIndexes = 0;
	tess.verts = nullptr;
	tess.indexes = nullptr;
	tess.buildingVBO = false;

	ri.Hunk_FreeTempMemory( vboIdxs );
	ri.Hunk_FreeTempMemory( vboVerts );

	if ( r_mergeLeafSurfaces->integer )
	{
		// count merged/unmerged surfaces
		int numUnmergedSurfaces = 0;
		int numMergedSurfaces = 0;
		int oldViewCount = -2;

		for ( i = 0; i < numSurfaces; i++ )
		{
			surface = surfaces[ i ];

			if ( surface->viewCount == -1 )
			{
				numUnmergedSurfaces++;
			}
			else if ( surface->viewCount != oldViewCount )
			{
				oldViewCount = surface->viewCount;
				numMergedSurfaces++;
			}
		}

		// Allocate merged surfaces
		s_worldData.mergedSurfaces = ( bspSurface_t * ) ri.Hunk_Alloc( sizeof( *s_worldData.mergedSurfaces ) * numMergedSurfaces, ha_pref::h_low );

		// actually merge surfaces
		mergedSurf = s_worldData.mergedSurfaces;
		oldViewCount = -2;
		for ( i = 0; i < numSurfaces; i++ )
		{
			vec3_t bounds[ 2 ];
			int surfVerts = 0;
			int surfIndexes = 0;
			int firstIndex = numTriangles * 3;
			srfVBOMesh_t *vboSurf;
			bspSurface_t *surf1 = surfaces[ i ];

			// skip unmergable surfaces
			if ( surf1->viewCount == -1 )
			{
				continue;
			}

			// skip surfaces that have already been merged
			if ( surf1->viewCount == oldViewCount )
			{
				continue;
			}

			oldViewCount = surf1->viewCount;

			if ( *surf1->data == surfaceType_t::SF_FACE )
			{
				srfSurfaceFace_t *face = ( srfSurfaceFace_t * ) surf1->data;
				firstIndex = face->firstTriangle * 3;
			}
			else if ( *surf1->data == surfaceType_t::SF_TRIANGLES )
			{
				srfTriangles_t *tris = ( srfTriangles_t * ) surf1->data;
				firstIndex = tris->firstTriangle * 3;
			}
			else if ( *surf1->data == surfaceType_t::SF_GRID )
			{
				srfGridMesh_t *grid = ( srfGridMesh_t * ) surf1->data;
				firstIndex = grid->firstTriangle * 3;
			}

			// count verts and indexes and add bounds for the merged surface
			ClearBounds( bounds[ 0 ], bounds[ 1 ] );
			for ( j = i; j < numSurfaces; j++ )
			{
				bspSurface_t *surf2 = surfaces[ j ];

				// stop merging when we hit a surface that can't be merged
				if ( surf2->viewCount != surf1->viewCount )
				{
					break;
				}

				if ( *surf2->data == surfaceType_t::SF_FACE )
				{
					srfSurfaceFace_t *face = ( srfSurfaceFace_t * ) surf2->data;
					surfIndexes += face->numTriangles * 3;
					surfVerts += face->numVerts;
					BoundsAdd( bounds[ 0 ], bounds[ 1 ], face->bounds[ 0 ], face->bounds[ 1 ] );
				}
				else if ( *surf2->data == surfaceType_t::SF_TRIANGLES )
				{
					srfTriangles_t *tris = ( srfTriangles_t * ) surf2->data;
					surfIndexes += tris->numTriangles * 3;
					surfVerts += tris->numVerts;
					BoundsAdd( bounds[ 0 ], bounds[ 1 ], tris->bounds[ 0 ], tris->bounds[ 1 ] );
				}
				else if ( *surf2->data == surfaceType_t::SF_GRID )
				{
					srfGridMesh_t *grid = ( srfGridMesh_t * ) surf2->data;
					surfIndexes += grid->numTriangles * 3;
					surfVerts += grid->numVerts;
					BoundsAdd( bounds[ 0 ], bounds[ 1 ], grid->bounds[ 0 ], grid->bounds[ 1 ] );
				}
			}

			if ( !surfIndexes || !surfVerts )
			{
				continue;
			}

			vboSurf = ( srfVBOMesh_t * ) ri.Hunk_Alloc( sizeof( *vboSurf ), ha_pref::h_low );
			memset( vboSurf, 0, sizeof( *vboSurf ) );
			vboSurf->surfaceType = surfaceType_t::SF_VBO_MESH;

			vboSurf->numIndexes = surfIndexes;
			vboSurf->numVerts = surfVerts;
			vboSurf->firstIndex = firstIndex;

			vboSurf->shader = surf1->shader;
			vboSurf->fogIndex = surf1->fogIndex;
			vboSurf->lightmapNum = surf1->lightmapNum;
			vboSurf->vbo = s_worldData.vbo;
			vboSurf->ibo = s_worldData.ibo;

			VectorCopy( bounds[ 0 ], vboSurf->bounds[ 0 ] );
			VectorCopy( bounds[ 1 ], vboSurf->bounds[ 1 ] );
			SphereFromBounds( vboSurf->bounds[ 0 ], vboSurf->bounds[ 1 ], vboSurf->origin, &vboSurf->radius );

			mergedSurf->data = ( surfaceType_t * ) vboSurf;
			mergedSurf->fogIndex = surf1->fogIndex;
			mergedSurf->shader = surf1->shader;
			mergedSurf->lightmapNum = surf1->lightmapNum;
			mergedSurf->viewCount = -1;

			// redirect view surfaces to this surf
			for ( k = 0; k < s_worldData.numMarkSurfaces; k++ )
			{
				bspSurface_t **view = s_worldData.viewSurfaces + k;

				if ( ( *view )->viewCount == surf1->viewCount )
				{
					*view = mergedSurf;
				}
			}

			mergedSurf++;
		}

		Log::Debug("Processed %d surfaces into %d merged, %d unmerged", numSurfaces, numMergedSurfaces, numUnmergedSurfaces );
	}

	endTime = ri.Milliseconds();
	Log::Debug("world VBO calculation time = %5.2f seconds", ( endTime - startTime ) / 1000.0 );

	// point triangle surfaces to world VBO
	for ( k = 0; k < numSurfaces; k++ )
	{
		surface = surfaces[ k ];

		if ( *surface->data == surfaceType_t::SF_FACE )
		{
			srfSurfaceFace_t *srf = ( srfSurfaceFace_t * ) surface->data;
			srf->vbo = s_worldData.vbo;
			srf->ibo = s_worldData.ibo;
		}
		else if ( *surface->data == surfaceType_t::SF_GRID )
		{
			srfGridMesh_t *srf = ( srfGridMesh_t * ) surface->data;
			srf->vbo = s_worldData.vbo;
			srf->ibo = s_worldData.ibo;
		}
		else if ( *surface->data == surfaceType_t::SF_TRIANGLES )
		{
			srfTriangles_t *srf = ( srfTriangles_t * ) surface->data;
			srf->vbo = s_worldData.vbo;
			srf->ibo = s_worldData.ibo;
		}

		// clear data used for sorting
		surface->viewCount = -1;
		surface->lightCount = -1;
		surface->interactionBits = 0;
	}

	ri.Hunk_FreeTempMemory( surfaces );
}

/*
===============
R_LoadSurfaces
===============
*/
static void R_LoadSurfaces( lump_t *surfs, lump_t *verts, lump_t *indexLump )
{
	dsurface_t   *in;
	bspSurface_t *out;
	drawVert_t   *dv;
	int          *indexes;
	int          count;
	int          numFaces, numMeshes, numTriSurfs, numFlares, numFoliages;
	int          i;

	Log::Debug("...loading surfaces" );

	numFaces = 0;
	numMeshes = 0;
	numTriSurfs = 0;
	numFlares = 0;
	numFoliages = 0;

	in = ( dsurface_t * )( fileBase + surfs->fileofs );

	if ( surfs->filelen % sizeof( *in ) )
	{
		Sys::Drop( "LoadMap: funny lump size in %s", s_worldData.name );
	}

	count = surfs->filelen / sizeof( *in );

	dv = ( drawVert_t * )( fileBase + verts->fileofs );

	if ( verts->filelen % sizeof( *dv ) )
	{
		Sys::Drop( "LoadMap: funny lump size in %s", s_worldData.name );
	}

	indexes = ( int * )( fileBase + indexLump->fileofs );

	if ( indexLump->filelen % sizeof( *indexes ) )
	{
		Sys::Drop( "LoadMap: funny lump size in %s", s_worldData.name );
	}

	out = (bspSurface_t*) ri.Hunk_Alloc( count * sizeof( *out ), ha_pref::h_low );

	s_worldData.surfaces = out;
	s_worldData.numSurfaces = count;

	for ( i = 0; i < count; i++, in++, out++ )
	{
		switch ( LittleLong( in->surfaceType ) )
		{
			case mapSurfaceType_t::MST_PATCH:
				ParseMesh( in, dv, out );
				numMeshes++;
				break;

			case mapSurfaceType_t::MST_TRIANGLE_SOUP:
				ParseTriSurf( in, dv, out, indexes );
				numTriSurfs++;
				break;

			case mapSurfaceType_t::MST_PLANAR:
				ParseFace( in, dv, out, indexes );
				numFaces++;
				break;

			case mapSurfaceType_t::MST_FLARE:
				ParseFlare( in, out );
				numFlares++;
				break;

			case mapSurfaceType_t::MST_FOLIAGE:
				// Tr3B: TODO ParseFoliage
				ParseTriSurf( in, dv, out, indexes );
				numFoliages++;
				break;

			default:
				Sys::Drop( "Bad surfaceType" );
		}
	}

	Log::Debug("...loaded %d faces, %i meshes, %i trisurfs, %i flares %i foliages", numFaces, numMeshes, numTriSurfs,
	           numFlares, numFoliages );

	if ( r_stitchCurves->integer )
	{
		R_StitchAllPatches();
	}

	R_FixSharedVertexLodError();

	if ( r_stitchCurves->integer )
	{
		R_MovePatchSurfacesToHunk();
	}
}

/*
=================
R_LoadSubmodels
=================
*/
static void R_LoadSubmodels( lump_t *l )
{
	dmodel_t   *in;
	bspModel_t *out;
	int        i, j, count;

	Log::Debug("...loading submodels" );

	in = ( dmodel_t * )( fileBase + l->fileofs );

	if ( l->filelen % sizeof( *in ) )
	{
		Sys::Drop( "LoadMap: funny lump size in %s", s_worldData.name );
	}

	count = l->filelen / sizeof( *in );

	s_worldData.numModels = count;
	s_worldData.models = out = (bspModel_t*) ri.Hunk_Alloc( count * sizeof( *out ), ha_pref::h_low );

	for ( i = 0; i < count; i++, in++, out++ )
	{
		model_t *model;

		model = R_AllocModel();

		ASSERT(model != nullptr);

		if ( model == nullptr )
		{
			Sys::Drop( "R_LoadSubmodels: R_AllocModel() failed" );
		}

		model->type = modtype_t::MOD_BSP;
		model->bsp = out;
		Com_sprintf( model->name, sizeof( model->name ), "*%d", i );

		for ( j = 0; j < 3; j++ )
		{
			out->bounds[ 0 ][ j ] = LittleFloat( in->mins[ j ] );
			out->bounds[ 1 ][ j ] = LittleFloat( in->maxs[ j ] );
		}

		out->firstSurface = s_worldData.surfaces + LittleLong( in->firstSurface );
		out->numSurfaces = LittleLong( in->numSurfaces );

		// ydnar: allocate decal memory
		j = ( i == 0 ? MAX_WORLD_DECALS : MAX_ENTITY_DECALS );
		out->decals = (decal_t*) ri.Hunk_Alloc( j * sizeof( *out->decals ), ha_pref::h_low );
		memset( out->decals, 0, j * sizeof( *out->decals ) );
	}
}

//==================================================================

/*
=================
R_SetParent
=================
*/
static void R_SetParent( bspNode_t *node, bspNode_t *parent )
{
	node->parent = parent;

	if ( node->contents != CONTENTS_NODE )
	{
		return;
	}

	R_SetParent( node->children[ 0 ], node );
	R_SetParent( node->children[ 1 ], node );
}

/*
=================
R_LoadNodesAndLeafs
=================
*/
static void R_LoadNodesAndLeafs( lump_t *nodeLump, lump_t *leafLump )
{
	int           i, j, p;
	dnode_t       *in;
	dleaf_t       *inLeaf;
	bspNode_t     *out;
	int           numNodes, numLeafs;
	vboData_t     data;

	Log::Debug("...loading nodes and leaves" );

	memset( &data, 0, sizeof( data ) );

	in = ( dnode_t * ) ( void * )( fileBase + nodeLump->fileofs );

	if ( nodeLump->filelen % sizeof( dnode_t ) || leafLump->filelen % sizeof( dleaf_t ) )
	{
		Sys::Drop( "LoadMap: funny lump size in %s", s_worldData.name );
	}

	numNodes = nodeLump->filelen / sizeof( dnode_t );
	numLeafs = leafLump->filelen / sizeof( dleaf_t );

	out = (bspNode_t*) ri.Hunk_Alloc( ( numNodes + numLeafs ) * sizeof( *out ), ha_pref::h_low );

	s_worldData.nodes = out;
	s_worldData.numnodes = numNodes + numLeafs;
	s_worldData.numDecisionNodes = numNodes;

	// ydnar: skybox optimization
	s_worldData.numSkyNodes = 0;
	s_worldData.skyNodes = (bspNode_t**) ri.Hunk_Alloc( WORLD_MAX_SKY_NODES * sizeof( *s_worldData.skyNodes ), ha_pref::h_low );

	// load nodes
	for ( i = 0; i < numNodes; i++, in++, out++ )
	{
		for ( j = 0; j < 3; j++ )
		{
			out->mins[ j ] = LittleLong( in->mins[ j ] );
			out->maxs[ j ] = LittleLong( in->maxs[ j ] );
		}

		p = LittleLong( in->planeNum );
		out->plane = s_worldData.planes + p;

		out->contents = CONTENTS_NODE; // differentiate from leafs

		for ( j = 0; j < 2; j++ )
		{
			p = LittleLong( in->children[ j ] );

			if ( p >= 0 )
			{
				out->children[ j ] = s_worldData.nodes + p;
			}
			else
			{
				out->children[ j ] = s_worldData.nodes + numNodes + ( -1 - p );
			}
		}
	}

	// load leafs
	inLeaf = ( dleaf_t * )( fileBase + leafLump->fileofs );

	for ( i = 0; i < numLeafs; i++, inLeaf++, out++ )
	{
		for ( j = 0; j < 3; j++ )
		{
			out->mins[ j ] = LittleLong( inLeaf->mins[ j ] );
			out->maxs[ j ] = LittleLong( inLeaf->maxs[ j ] );
		}

		out->cluster = LittleLong( inLeaf->cluster );
		out->area = LittleLong( inLeaf->area );

		if ( out->cluster >= s_worldData.numClusters )
		{
			s_worldData.numClusters = out->cluster + 1;
		}

		out->firstMarkSurface = LittleLong( inLeaf->firstLeafSurface );
		out->numMarkSurfaces = LittleLong( inLeaf->numLeafSurfaces );
	}

	// chain descendants and compute surface bounds
	R_SetParent( s_worldData.nodes, nullptr );

	backEndData[ 0 ]->traversalList = ( bspNode_t ** ) ri.Hunk_Alloc( sizeof( bspNode_t * ) * s_worldData.numnodes, ha_pref::h_low );
	backEndData[ 0 ]->traversalLength = 0;

	if ( r_smp->integer )
	{
		backEndData[ 1 ]->traversalList = ( bspNode_t ** ) ri.Hunk_Alloc( sizeof( bspNode_t * ) * s_worldData.numnodes, ha_pref::h_low );
		backEndData[ 1 ]->traversalLength = 0;
	}
}

//=============================================================================

/*
=================
R_LoadShaders
=================
*/
static void R_LoadShaders( lump_t *l )
{
	int       i, count;
	dshader_t *in, *out;

	Log::Debug("...loading shaders" );

	in = ( dshader_t * )( fileBase + l->fileofs );

	if ( l->filelen % sizeof( *in ) )
	{
		Sys::Drop( "LoadMap: funny lump size in %s", s_worldData.name );
	}

	count = l->filelen / sizeof( *in );
	out = (dshader_t*) ri.Hunk_Alloc( count * sizeof( *out ), ha_pref::h_low );

	s_worldData.shaders = out;
	s_worldData.numShaders = count;

	memcpy( out, in, count * sizeof( *out ) );

	for ( i = 0; i < count; i++ )
	{
		Log::Debug("loading shader: '%s'", out[ i ].shader );

		out[ i ].surfaceFlags = LittleLong( out[ i ].surfaceFlags );
		out[ i ].contentFlags = LittleLong( out[ i ].contentFlags );
	}
}

/*
=================
R_LoadMarksurfaces
=================
*/
static void R_LoadMarksurfaces( lump_t *l )
{
	int          i, j, count;
	int          *in;
	bspSurface_t **out;

	Log::Debug("...loading mark surfaces" );

	in = ( int * )( fileBase + l->fileofs );

	if ( l->filelen % sizeof( *in ) )
	{
		Sys::Drop( "LoadMap: funny lump size in %s", s_worldData.name );
	}

	count = l->filelen / sizeof( *in );
	out = (bspSurface_t**) ri.Hunk_Alloc( count * sizeof( *out ), ha_pref::h_low );

	s_worldData.markSurfaces = out;
	s_worldData.numMarkSurfaces = count;
	s_worldData.viewSurfaces = ( bspSurface_t ** ) ri.Hunk_Alloc( count * sizeof( *out ), ha_pref::h_low );

	for ( i = 0; i < count; i++ )
	{
		j = LittleLong( in[ i ] );
		if ( j < 0 || j >= s_worldData.numSurfaces )
		{
			Sys::Drop( "LoadMap: invalid surface number %d", j );
		}
		out[ i ] = s_worldData.surfaces + j;
		s_worldData.viewSurfaces[ i ] = out[ i ];
	}
}

/*
=================
R_LoadPlanes
=================
*/
static void R_LoadPlanes( lump_t *l )
{
	int      i, j;
	cplane_t *out;
	dplane_t *in;
	int      count;

	Log::Debug("...loading planes" );

	in = ( dplane_t * )( fileBase + l->fileofs );

	if ( l->filelen % sizeof( *in ) )
	{
		Sys::Drop( "LoadMap: funny lump size in %s", s_worldData.name );
	}

	count = l->filelen / sizeof( *in );
	out = (cplane_t*) ri.Hunk_Alloc( count * 2 * sizeof( *out ), ha_pref::h_low );

	s_worldData.planes = out;
	s_worldData.numplanes = count;

	for ( i = 0; i < count; i++, in++, out++ )
	{
		for ( j = 0; j < 3; j++ )
		{
			out->normal[ j ] = LittleFloat( in->normal[ j ] );
		}

		out->dist = LittleFloat( in->dist );
		out->type = PlaneTypeForNormal( out->normal );
		SetPlaneSignbits( out );
	}
}

/*
=================
R_LoadFogs
=================
*/
static void R_LoadFogs( lump_t *l, lump_t *brushesLump, lump_t *sidesLump )
{
	int          i;
	fog_t        *out;
	dfog_t       *fogs;
	dbrush_t     *brushes, *brush;
	dbrushside_t *sides;
	int          count, brushesCount, sidesCount;
	int          sideNum;
	int          planeNum;
	shader_t     *shader;
	float        d;
	int          firstSide = 0;

	Log::Debug("...loading fogs" );

	fogs = ( dfog_t* )( fileBase + l->fileofs );

	if ( l->filelen % sizeof( *fogs ) )
	{
		Sys::Drop( "LoadMap: funny lump size in %s", s_worldData.name );
	}

	count = l->filelen / sizeof( *fogs );

	// create fog structures for them
	s_worldData.numFogs = count + 1;
	s_worldData.fogs = (fog_t*) ri.Hunk_Alloc( s_worldData.numFogs * sizeof( *out ), ha_pref::h_low );
	out = s_worldData.fogs + 1;

	// ydnar: reset global fog
	s_worldData.globalFog = -1;

	if ( !count )
	{
		Log::Debug("no fog volumes loaded" );
		return;
	}

	brushes = ( dbrush_t * )( fileBase + brushesLump->fileofs );

	if ( brushesLump->filelen % sizeof( *brushes ) )
	{
		Sys::Drop( "LoadMap: funny lump size in %s", s_worldData.name );
	}

	brushesCount = brushesLump->filelen / sizeof( *brushes );

	sides = ( dbrushside_t * )( fileBase + sidesLump->fileofs );

	if ( sidesLump->filelen % sizeof( *sides ) )
	{
		Sys::Drop( "LoadMap: funny lump size in %s", s_worldData.name );
	}

	sidesCount = sidesLump->filelen / sizeof( *sides );

	for ( i = 0; i < count; i++, fogs++ )
	{
		out->originalBrushNumber = LittleLong( fogs->brushNum );

		// ydnar: global fog has a brush number of -1, and no visible side
		if ( out->originalBrushNumber == -1 )
		{
			VectorSet( out->bounds[ 0 ], MIN_WORLD_COORD, MIN_WORLD_COORD, MIN_WORLD_COORD );
			VectorSet( out->bounds[ 1 ], MAX_WORLD_COORD, MAX_WORLD_COORD, MAX_WORLD_COORD );
		}
		else
		{
			if ( out->originalBrushNumber >= brushesCount )
			{
				Sys::Drop( "fog brushNumber out of range" );
			}

			brush = brushes + out->originalBrushNumber;

			firstSide = LittleLong( brush->firstSide );

			if ( firstSide > sidesCount - 6 )
			{
				Sys::Drop( "fog brush sideNumber out of range" );
			}

			// brushes are always sorted with the axial sides first
			sideNum = firstSide + 0;
			planeNum = LittleLong( sides[ sideNum ].planeNum );
			out->bounds[ 0 ][ 0 ] = -s_worldData.planes[ planeNum ].dist;

			sideNum = firstSide + 1;
			planeNum = LittleLong( sides[ sideNum ].planeNum );
			out->bounds[ 1 ][ 0 ] = s_worldData.planes[ planeNum ].dist;

			sideNum = firstSide + 2;
			planeNum = LittleLong( sides[ sideNum ].planeNum );
			out->bounds[ 0 ][ 1 ] = -s_worldData.planes[ planeNum ].dist;

			sideNum = firstSide + 3;
			planeNum = LittleLong( sides[ sideNum ].planeNum );
			out->bounds[ 1 ][ 1 ] = s_worldData.planes[ planeNum ].dist;

			sideNum = firstSide + 4;
			planeNum = LittleLong( sides[ sideNum ].planeNum );
			out->bounds[ 0 ][ 2 ] = -s_worldData.planes[ planeNum ].dist;

			sideNum = firstSide + 5;
			planeNum = LittleLong( sides[ sideNum ].planeNum );
			out->bounds[ 1 ][ 2 ] = s_worldData.planes[ planeNum ].dist;
		}

		// get information from the shader for fog parameters
		shader = R_FindShader( fogs->shader, shaderType_t::SHADER_3D_DYNAMIC, RSF_DEFAULT );

		out->fogParms = shader->fogParms;

		out->color = Color::Adapt( shader->fogParms.color );

		/* Historically it was done:

			out->color *= tr.identityLight;

		But tr.identityLight is always 1.0f in Dæmon engine
		as the as the overbright bit implementation is fully
		software. */

		out->color.SetAlpha( 1 );

		d = shader->fogParms.depthForOpaque < 1 ? 1 : shader->fogParms.depthForOpaque;
		out->tcScale = 1.0f / ( d * 8 );

		// ydnar: global fog sets clearcolor/zfar
		if ( out->originalBrushNumber == -1 )
		{
			s_worldData.globalFog = i + 1;
			VectorCopy( shader->fogParms.color, s_worldData.globalOriginalFog );
			s_worldData.globalOriginalFog[ 3 ] = shader->fogParms.depthForOpaque;
		}

		// set the gradient vector
		sideNum = LittleLong( fogs->visibleSide );

		// ydnar: made this check a little more strenuous (was sideNum == -1)
		if ( sideNum < 0 || sideNum >= sidesCount )
		{
			out->hasSurface = false;
		}
		else
		{
			out->hasSurface = true;
			planeNum = LittleLong( sides[ firstSide + sideNum ].planeNum );
			VectorSubtract( vec3_origin, s_worldData.planes[ planeNum ].normal, out->surface );
			out->surface[ 3 ] = -s_worldData.planes[ planeNum ].dist;
		}

		out++;
	}

	Log::Debug("%i fog volumes loaded", s_worldData.numFogs );
}

static void R_SetDefaultLightGrid()
{
	world_t *w = &s_worldData;

	// generate default 1x1x1 light grid
	w->lightGridSize[ 0 ] = 100000.0f;
	w->lightGridSize[ 1 ] = 100000.0f;
	w->lightGridSize[ 2 ] = 100000.0f;
	w->lightGridInverseSize[ 0 ] = 1.0f / w->lightGridSize[ 0 ];
	w->lightGridInverseSize[ 1 ] = 1.0f / w->lightGridSize[ 1 ];
	w->lightGridInverseSize[ 2 ] = 1.0f / w->lightGridSize[ 2 ];

	VectorSet( w->lightGridOrigin, 0.0f, 0.0f, 0.0f );

	VectorMA( w->lightGridOrigin, -0.5f, w->lightGridSize,
		  w->lightGridGLOrigin );

	VectorSet( w->lightGridBounds, 1, 1, 1 );
	w->numLightGridPoints = 1;

	w->lightGridGLScale[ 0 ] = w->lightGridInverseSize[ 0 ];
	w->lightGridGLScale[ 1 ] = w->lightGridInverseSize[ 1 ];
	w->lightGridGLScale[ 2 ] = w->lightGridInverseSize[ 2 ];

	bspGridPoint1_t *gridPoint1 = (bspGridPoint1_t *) ri.Hunk_Alloc( sizeof( bspGridPoint1_t ) + sizeof( bspGridPoint2_t ), ha_pref::h_low );
	bspGridPoint2_t *gridPoint2 = (bspGridPoint2_t *) (gridPoint1 + w->numLightGridPoints);

	// default some white light from above
	gridPoint1->color[ 0 ] = 64;
	gridPoint1->color[ 1 ] = 64;
	gridPoint1->color[ 2 ] = 64;
	gridPoint1->ambientPart = 128;
	gridPoint2->direction[ 0 ] = floatToSnorm8(0.0f);
	gridPoint2->direction[ 1 ] = floatToSnorm8(0.0f);
	gridPoint2->direction[ 2 ] = floatToSnorm8(1.0f);
	gridPoint2->unused = 0;

	w->lightGridData1 = gridPoint1;
	w->lightGridData2 = gridPoint2;

	imageParams_t imageParams = {};
	imageParams.bits = IF_NOPICMIP | IF_NOLIGHTSCALE;
	imageParams.filterType = filterType_t::FT_DEFAULT;
	imageParams.wrapType = wrapTypeEnum_t::WT_EDGE_CLAMP;

	tr.lightGrid1Image = R_Create3DImage("<lightGrid1>", (const byte *)w->lightGridData1, w->lightGridBounds[ 0 ], w->lightGridBounds[ 1 ], w->lightGridBounds[ 2 ], imageParams );
	tr.lightGrid2Image = R_Create3DImage("<lightGrid2>", (const byte *)w->lightGridData2, w->lightGridBounds[ 0 ], w->lightGridBounds[ 1 ], w->lightGridBounds[ 2 ], imageParams );
}

/*
================
R_LoadLightGrid
================
*/
void R_LoadLightGrid( lump_t *l )
{
	int            i, j, k;
	vec3_t         maxs;
	world_t        *w;
	float          *wMins, *wMaxs;
	dgridPoint_t   *in;
	bspGridPoint1_t *gridPoint1;
	bspGridPoint2_t *gridPoint2;
	float          lat, lng;
	int            from[ 3 ], to[ 3 ];
	float          weights[ 3 ] = { 0.25f, 0.5f, 0.25f };
	float          *factors[ 3 ] = { weights, weights, weights };
	vec3_t         ambientColor, directedColor, direction;
	float          scale;

	if ( !r_precomputedLighting->integer )
	{
		R_SetDefaultLightGrid();
		return;
	}

	Log::Debug("...loading light grid" );

	w = &s_worldData;

	w->lightGridInverseSize[ 0 ] = 1.0f / w->lightGridSize[ 0 ];
	w->lightGridInverseSize[ 1 ] = 1.0f / w->lightGridSize[ 1 ];
	w->lightGridInverseSize[ 2 ] = 1.0f / w->lightGridSize[ 2 ];

	wMins = w->models[ 0 ].bounds[ 0 ];
	wMaxs = w->models[ 0 ].bounds[ 1 ];

	for ( i = 0; i < 3; i++ )
	{
		w->lightGridOrigin[ i ] = w->lightGridSize[ i ] * ceil( wMins[ i ] / w->lightGridSize[ i ] );
		maxs[ i ] = w->lightGridSize[ i ] * floor( wMaxs[ i ] / w->lightGridSize[ i ] );
		w->lightGridBounds[ i ] = ( maxs[ i ] - w->lightGridOrigin[ i ] ) / w->lightGridSize[ i ] + 1;
	}

	VectorMA( w->lightGridOrigin, -0.5f, w->lightGridSize,
		  w->lightGridGLOrigin );

	w->lightGridGLScale[ 0 ] = w->lightGridInverseSize[ 0 ] / w->lightGridBounds[ 0 ];
	w->lightGridGLScale[ 1 ] = w->lightGridInverseSize[ 1 ] / w->lightGridBounds[ 1 ];
	w->lightGridGLScale[ 2 ] = w->lightGridInverseSize[ 2 ] / w->lightGridBounds[ 2 ];

	w->numLightGridPoints = w->lightGridBounds[ 0 ] * w->lightGridBounds[ 1 ] * w->lightGridBounds[ 2 ];

	Log::Debug("grid size (%i %i %i)", ( int ) w->lightGridSize[ 0 ], ( int ) w->lightGridSize[ 1 ],
	           ( int ) w->lightGridSize[ 2 ] );
	Log::Debug("grid bounds (%i %i %i)", w->lightGridBounds[ 0 ], w->lightGridBounds[ 1 ],
			   w->lightGridBounds[ 2 ]);

	if ( static_cast<size_t>(l->filelen) != w->numLightGridPoints * sizeof( dgridPoint_t ) )
	{
		Log::Warn("light grid mismatch, default light grid used" );

		R_SetDefaultLightGrid();

		return;
	}

	in = ( dgridPoint_t * )( fileBase + l->fileofs );

	if ( l->filelen % sizeof( *in ) )
	{
		Sys::Drop( "LoadMap: funny lump size in %s", s_worldData.name );
	}

	i = w->numLightGridPoints * ( sizeof( *gridPoint1 ) + sizeof( *gridPoint2 ) );
	gridPoint1 = (bspGridPoint1_t *) ri.Hunk_Alloc( i, ha_pref::h_low );
	gridPoint2 = (bspGridPoint2_t *) (gridPoint1 + w->numLightGridPoints);

	w->lightGridData1 = gridPoint1;
	w->lightGridData2 = gridPoint2;

	for ( i = 0; i < w->numLightGridPoints;
	      i++, in++, gridPoint1++, gridPoint2++ )
	{
		byte tmpAmbient[ 4 ];
		byte tmpDirected[ 4 ];

		tmpAmbient[ 0 ] = in->ambient[ 0 ];
		tmpAmbient[ 1 ] = in->ambient[ 1 ];
		tmpAmbient[ 2 ] = in->ambient[ 2 ];
		tmpAmbient[ 3 ] = 255;

		tmpDirected[ 0 ] = in->directed[ 0 ];
		tmpDirected[ 1 ] = in->directed[ 1 ];
		tmpDirected[ 2 ] = in->directed[ 2 ];
		tmpDirected[ 3 ] = 255;

		R_ColorShiftLightingBytes( tmpAmbient, tmpAmbient );
		R_ColorShiftLightingBytes( tmpDirected, tmpDirected );

		for ( j = 0; j < 3; j++ )
		{
			ambientColor[ j ] = tmpAmbient[ j ] * ( 1.0f / 255.0f );
			directedColor[ j ] = tmpDirected[ j ] * ( 1.0f / 255.0f );
		}

		// standard spherical coordinates to cartesian coordinates conversion

		// decode X as cos( lat ) * sin( long )
		// decode Y as sin( lat ) * sin( long )
		// decode Z as cos( long )

		// RB: having a look in NormalToLatLong used by q3map2 shows the order of latLong

		// Lat = 0 at (1,0,0) to 360 (-1,0,0), encoded in 8-bit sine table format
		// Lng = 0 at (0,0,1) to 180 (0,0,-1), encoded in 8-bit sine table format

		lat = DEG2RAD( in->latLong[ 1 ] * ( 360.0f / 255.0f ) );
		lng = DEG2RAD( in->latLong[ 0 ] * ( 360.0f / 255.0f ) );

		direction[ 0 ] = cosf( lat ) * sinf( lng );
		direction[ 1 ] = sinf( lat ) * sinf( lng );
		direction[ 2 ] = cosf( lng );

		// Pack data into an bspGridPoint
		gridPoint1->color[ 0 ] = floatToUnorm8( 0.5f * (ambientColor[ 0 ] + directedColor[ 0 ]) );
		gridPoint1->color[ 1 ] = floatToUnorm8( 0.5f * (ambientColor[ 1 ] + directedColor[ 1 ]) );
		gridPoint1->color[ 2 ] = floatToUnorm8( 0.5f * (ambientColor[ 2 ] + directedColor[ 2 ]) );

		// Avoid division-by-zero.
		float ambientLength = VectorLength(ambientColor);
		float directedLength = VectorLength(directedColor);
		float length = ambientLength + directedLength;
		gridPoint1->ambientPart = length ? floatToUnorm8( ambientLength / length ) : 0;

		gridPoint2->direction[0] = 128 + floatToSnorm8( direction[ 0 ] );
		gridPoint2->direction[1] = 128 + floatToSnorm8( direction[ 1 ] );
		gridPoint2->direction[2] = 128 + floatToSnorm8( direction[ 2 ] );
		gridPoint2->unused = 0;
	}

	// fill in gridpoints with zero light (samples in walls) to avoid
	// darkening of objects near walls
	gridPoint1 = w->lightGridData1;
	gridPoint2 = w->lightGridData2;

	for( k = 0; k < w->lightGridBounds[ 2 ]; k++ ) {
		from[ 2 ] = k - 1;
		to[ 2 ] = k + 1;

		for( j = 0; j < w->lightGridBounds[ 1 ]; j++ ) {
			from[ 1 ] = j - 1;
			to[ 1 ] = j + 1;

			for( i = 0; i < w->lightGridBounds[ 0 ];
			     i++, gridPoint1++, gridPoint2++ ) {
				from[ 0 ] = i - 1;
				to[ 0 ] = i + 1;

				if( gridPoint1->color[ 0 ] ||
				    gridPoint1->color[ 1 ] ||
				    gridPoint1->color[ 2 ] )
					continue;

				scale = R_InterpolateLightGrid( w, from, to, factors,
								ambientColor, directedColor,
								direction );
				if( scale > 0.0f ) {
					scale = 1.0f / scale;

					VectorScale( ambientColor, scale, ambientColor );
					VectorScale( directedColor, scale, directedColor );
					VectorScale( direction, scale, direction );


					gridPoint1->color[0] = floatToUnorm8(0.5f * (ambientColor[0] + directedColor[0]));
					gridPoint1->color[1] = floatToUnorm8(0.5f * (ambientColor[1] + directedColor[1]));
					gridPoint1->color[2] = floatToUnorm8(0.5f * (ambientColor[2] + directedColor[2]));
					gridPoint1->ambientPart = floatToUnorm8(VectorLength(ambientColor) / (VectorLength(ambientColor) + VectorLength(directedColor)));

					gridPoint2->direction[0] = 128 + floatToSnorm8(direction[0]);
					gridPoint2->direction[1] = 128 + floatToSnorm8(direction[1]);
					gridPoint2->direction[2] = 128 + floatToSnorm8(direction[2]);
					gridPoint2->unused = 0;
				}
			}
		}
	}

	imageParams_t imageParams = {};
	imageParams.bits = IF_NOPICMIP | IF_NOLIGHTSCALE;
	imageParams.filterType = filterType_t::FT_LINEAR;
	imageParams.wrapType = wrapTypeEnum_t::WT_EDGE_CLAMP;

	tr.lightGrid1Image = R_Create3DImage("<lightGrid1>", (const byte *)w->lightGridData1, w->lightGridBounds[ 0 ], w->lightGridBounds[ 1 ], w->lightGridBounds[ 2 ], imageParams );
	tr.lightGrid2Image = R_Create3DImage("<lightGrid2>", (const byte *)w->lightGridData2, w->lightGridBounds[ 0 ], w->lightGridBounds[ 1 ], w->lightGridBounds[ 2 ], imageParams );

	Log::Debug("%i light grid points created", w->numLightGridPoints );
}

/*
================
R_LoadEntities
================
*/
void R_LoadEntities( lump_t *l )
{
	int          i;
	const char *p, *pOld, *token;
	char *s;
	char         keyname[ MAX_TOKEN_CHARS ];
	char         value[ MAX_TOKEN_CHARS ];
	world_t      *w;
	bool     isLight = false;
	int          numEntities = 0;
	int          numLights = 0;
	int          numOmniLights = 0;
	int          numProjLights = 0;
	int          numParallelLights = 0;
	trRefLight_t *light;

	Log::Debug("...loading entities" );

	w = &s_worldData;
	w->lightGridSize[ 0 ] = 64;
	w->lightGridSize[ 1 ] = 64;
	w->lightGridSize[ 2 ] = 128;

	// store for reference by the cgame
	w->entityString = (char*) ri.Hunk_Alloc( l->filelen + 1, ha_pref::h_low );
	//strcpy(w->entityString, (char *)(fileBase + l->fileofs));
	Q_strncpyz( w->entityString, ( char * )( fileBase + l->fileofs ), l->filelen + 1 );
	w->entityParsePoint = w->entityString;

	p = w->entityString;

	// only parse the world spawn
	while ( true )
	{
		// parse key
		token = COM_ParseExt2( &p, true );

		if ( !*token )
		{
			Log::Warn("unexpected end of entities string while parsing worldspawn" );
			break;
		}

		if ( *token == '{' )
		{
			continue;
		}

		if ( *token == '}' )
		{
			break;
		}

		Q_strncpyz( keyname, token, sizeof( keyname ) );

		// parse value
		token = COM_ParseExt2( &p, false );

		if ( !*token )
		{
			continue;
		}

		Q_strncpyz( value, token, sizeof( value ) );

		// check for remapping of shaders for vertex lighting
		s = (char *) "vertexremapshader";

		if ( !Q_strncmp( keyname, s, strlen( s ) ) )
		{
			s = strchr( value, ';' );

			if ( !s )
			{
				Log::Warn("no semi colon in vertexshaderremap '%s'", value );
				break;
			}

			*s++ = 0;
			continue;
		}

		// check for remapping of shaders
		s = (char *) "remapshader";

		if ( !Q_strncmp( keyname, s, strlen( s ) ) )
		{
			s = strchr( value, ';' );

			if ( !s )
			{
				Log::Warn("no semi colon in shaderremap '%s'", value );
				break;
			}

			*s++ = 0;
			R_RemapShader( value, s, "0" );
			continue;
		}

		// check for a different grid size
		if ( !Q_stricmp( keyname, "gridsize" ) )
		{
			sscanf( value, "%f %f %f", &w->lightGridSize[ 0 ], &w->lightGridSize[ 1 ], &w->lightGridSize[ 2 ] );
			continue;
		}

		// check for ambient color
		else if ( !Q_stricmp( keyname, "_color" ) || !Q_stricmp( keyname, "ambientColor" ) )
		{
			if ( r_forceAmbient->value <= 0 )
			{
				sscanf( value, "%f %f %f", &tr.worldEntity.ambientLight[ 0 ], &tr.worldEntity.ambientLight[ 1 ],
				        &tr.worldEntity.ambientLight[ 2 ] );

				VectorCopy( tr.worldEntity.ambientLight, tr.worldEntity.ambientLight );
				VectorScale( tr.worldEntity.ambientLight, r_ambientScale->value, tr.worldEntity.ambientLight );
			}
		}

		// check for deluxe mapping support
		if ( !Q_stricmp( keyname, "deluxeMapping" ) && !Q_stricmp( value, "1" ) )
		{
			Log::Debug("map features directional light mapping" );
			// This will be disabled if the engine fails to load the lightmaps.
			tr.worldDeluxeMapping = r_deluxeMapping->integer != 0;
			continue;
		}

		// check for mapOverBrightBits override
		else if ( !Q_stricmp( keyname, "mapOverBrightBits" ) )
		{
			tr.mapOverBrightBits = Math::Clamp( atof( value ), 0.0, 3.0 );
		}

		// check for deluxe mapping provided by NetRadiant's q3map2
		if ( !Q_stricmp( keyname, "_q3map2_cmdline" ) )
		{
			s = strstr( value, "-deluxe" );

			if ( s )
			{
				Log::Debug("map features directional light mapping" );
				// This will be disabled if the engine fails to load the lightmaps.
				tr.worldDeluxeMapping = r_deluxeMapping->integer != 0;
			}

			continue;
		}

		// check for HDR light mapping support
		if ( !Q_stricmp( keyname, "hdrRGBE" ) && !Q_stricmp( value, "1" ) )
		{
			Log::Debug("map features HDR light mapping" );
			tr.worldHDR_RGBE = true;
			continue;
		}

		if ( !Q_stricmp( keyname, "classname" ) && Q_stricmp( value, "worldspawn" ) )
		{
			Log::Warn("expected worldspawn found '%s'", value );
			continue;
		}
	}

	pOld = p;
	numEntities = 1; // parsed worldspawn so far

	// count lights
	while ( true )
	{
		// parse {
		token = COM_ParseExt2( &p, true );

		if ( !*token )
		{
			// end of entities string
			break;
		}

		if ( *token != '{' )
		{
			Log::Warn("expected { found '%s'", token );
			break;
		}

		// new entity
		isLight = false;

		// parse epairs
		while ( true )
		{
			// parse key
			token = COM_ParseExt2( &p, true );

			if ( *token == '}' )
			{
				break;
			}

			if ( !*token )
			{
				Log::Warn("EOF without closing bracket" );
				break;
			}

			Q_strncpyz( keyname, token, sizeof( keyname ) );

			// parse value
			token = COM_ParseExt2( &p, false );

			if ( !*token )
			{
				Log::Warn("missing value for key '%s'", keyname );
				continue;
			}

			Q_strncpyz( value, token, sizeof( value ) );

			// check if this entity is a light
			if ( !Q_stricmp( keyname, "classname" ) && !Q_stricmp( value, "light" ) )
			{
				isLight = true;
			}
		}

		if ( *token != '}' )
		{
			Log::Warn("expected } found '%s'", token );
			break;
		}

		if ( isLight )
		{
			numLights++;
		}

		numEntities++;
	}

	Log::Debug("%i total entities counted", numEntities );
	Log::Debug("%i total lights counted", numLights );

	s_worldData.numLights = numLights;

	// Tr3B: FIXME add 1 dummy light so we don't trash the hunk memory system ...
	s_worldData.lights = (trRefLight_t*) ri.Hunk_Alloc( ( s_worldData.numLights + 1 ) * sizeof( trRefLight_t ), ha_pref::h_low );

	// basic light setup
	for ( i = 0, light = s_worldData.lights; i < s_worldData.numLights; i++, light++ )
	{
		QuatClear( light->l.rotation );
		VectorClear( light->l.center );

		light->l.color[ 0 ] = 1;
		light->l.color[ 1 ] = 1;
		light->l.color[ 2 ] = 1;

		light->l.scale = r_lightScale->value;

		light->l.radius = 300.0f;

		VectorClear( light->l.projTarget );
		VectorClear( light->l.projRight );
		VectorClear( light->l.projUp );
		VectorClear( light->l.projStart );
		VectorClear( light->l.projEnd );

		light->l.inverseShadows = false;

		light->isStatic = true;
		light->noRadiosity = false;
		light->additive = true;

		light->shadowLOD = 0;
	}

	// parse lights
	p = pOld;
	numEntities = 1;
	light = &s_worldData.lights[ 0 ];

	while ( true )
	{
		// parse {
		token = COM_ParseExt2( &p, true );

		if ( !*token )
		{
			// end of entities string
			break;
		}

		if ( *token != '{' )
		{
			Log::Warn("expected { found '%s'", token );
			break;
		}

		// new entity
		isLight = false;

		// parse epairs
		while ( true )
		{
			// parse key
			token = COM_ParseExt2( &p, true );

			if ( *token == '}' )
			{
				break;
			}

			if ( !*token )
			{
				Log::Warn("EOF without closing bracket" );
				break;
			}

			Q_strncpyz( keyname, token, sizeof( keyname ) );

			// parse value
			token = COM_ParseExt2( &p, false );

			if ( !*token )
			{
				Log::Warn("missing value for key '%s'", keyname );
				continue;
			}

			Q_strncpyz( value, token, sizeof( value ) );

			// check if this entity is a light
			if ( !Q_stricmp( keyname, "classname" ) && !Q_stricmp( value, "light" ) )
			{
				isLight = true;
			}
			// check for origin
			else if ( !Q_stricmp( keyname, "origin" ) || !Q_stricmp( keyname, "light_origin" ) )
			{
				sscanf( value, "%f %f %f", &light->l.origin[ 0 ], &light->l.origin[ 1 ], &light->l.origin[ 2 ] );
				s = &value[ 0 ];
			}
			// check for center
			else if ( !Q_stricmp( keyname, "light_center" ) )
			{
				sscanf( value, "%f %f %f", &light->l.center[ 0 ], &light->l.center[ 1 ], &light->l.center[ 2 ] );
				s = &value[ 0 ];
			}
			// check for color
			else if ( !Q_stricmp( keyname, "_color" ) )
			{
				sscanf( value, "%f %f %f", &light->l.color[ 0 ], &light->l.color[ 1 ], &light->l.color[ 2 ] );
				s = &value[ 0 ];
			}
			// check for radius
			else if ( !Q_stricmp( keyname, "light_radius" ) )
			{
				vec_t rad_y, rad_z;
				sscanf( value, "%f %f %f", &light->l.radius, &rad_y, &rad_z );
				if( rad_y != light->l.radius || rad_z != light->l.radius )
					Log::Warn( "non-spherical light" );
				s = &value[ 0 ];
			}
			// check for light_target
			else if ( !Q_stricmp( keyname, "light_target" ) )
			{
				sscanf( value, "%f %f %f", &light->l.projTarget[ 0 ], &light->l.projTarget[ 1 ], &light->l.projTarget[ 2 ] );
				s = &value[ 0 ];
				light->l.rlType = refLightType_t::RL_PROJ;
			}
			// check for light_right
			else if ( !Q_stricmp( keyname, "light_right" ) )
			{
				sscanf( value, "%f %f %f", &light->l.projRight[ 0 ], &light->l.projRight[ 1 ], &light->l.projRight[ 2 ] );
				s = &value[ 0 ];
				light->l.rlType = refLightType_t::RL_PROJ;
			}
			// check for light_up
			else if ( !Q_stricmp( keyname, "light_up" ) )
			{
				sscanf( value, "%f %f %f", &light->l.projUp[ 0 ], &light->l.projUp[ 1 ], &light->l.projUp[ 2 ] );
				s = &value[ 0 ];
				light->l.rlType = refLightType_t::RL_PROJ;
			}
			// check for light_start
			else if ( !Q_stricmp( keyname, "light_start" ) )
			{
				sscanf( value, "%f %f %f", &light->l.projStart[ 0 ], &light->l.projStart[ 1 ], &light->l.projStart[ 2 ] );
				s = &value[ 0 ];
				light->l.rlType = refLightType_t::RL_PROJ;
			}
			// check for light_end
			else if ( !Q_stricmp( keyname, "light_end" ) )
			{
				sscanf( value, "%f %f %f", &light->l.projEnd[ 0 ], &light->l.projEnd[ 1 ], &light->l.projEnd[ 2 ] );
				s = &value[ 0 ];
				light->l.rlType = refLightType_t::RL_PROJ;
			}
			// check for radius
			else if ( !Q_stricmp( keyname, "light" ) || !Q_stricmp( keyname, "_light" ) )
			{
				light->l.radius = atof( value );
			}
			// check for scale
			else if ( !Q_stricmp( keyname, "light_scale" ) )
			{
				light->l.scale = atof( value );

				if ( light->l.scale >= r_lightScale->value )
				{
					light->l.scale = r_lightScale->value;
				}
			}
			// check for light shader
			else if ( !Q_stricmp( keyname, "texture" ) )
			{
				light->l.attenuationShader = RE_RegisterShader( value, RSF_LIGHT_ATTENUATION );
			}
			// check for rotation
			else if ( !Q_stricmp( keyname, "rotation" ) || !Q_stricmp( keyname, "light_rotation" ) )
			{
				matrix_t rotation;

				sscanf( value, "%f %f %f %f %f %f %f %f %f", &rotation[ 0 ], &rotation[ 1 ], &rotation[ 2 ],
				        &rotation[ 4 ], &rotation[ 5 ], &rotation[ 6 ], &rotation[ 8 ], &rotation[ 9 ], &rotation[ 10 ] );

				QuatFromMatrix( light->l.rotation, rotation );
			}
			// check if this light does not cast any shadows
			else if ( !Q_stricmp( keyname, "noshadows" ) && !Q_stricmp( value, "1" ) )
			{
				light->l.noShadows = true;
			}
			// check if this light does not contribute to the global lightmapping
			else if ( !Q_stricmp( keyname, "noradiosity" ) && !Q_stricmp( value, "1" ) )
			{
				light->noRadiosity = true;
			}
			// check if this light is a parallel sun light
			else if ( !Q_stricmp( keyname, "parallel" ) && !Q_stricmp( value, "1" ) )
			{
				light->l.rlType = refLightType_t::RL_DIRECTIONAL;
			}
		}

		if ( *token != '}' )
		{
			Log::Warn("expected } found '%s'", token );
			break;
		}

		if ( !isLight )
		{
			// reset rotation because it may be set to the rotation of other entities
			QuatClear( light->l.rotation );
		}
		else
		{
			if ( ( numOmniLights + numProjLights + numParallelLights ) < s_worldData.numLights )
			{
				switch ( light->l.rlType )
				{
					case refLightType_t::RL_OMNI:
						numOmniLights++;
						break;

					case refLightType_t::RL_PROJ:
						numProjLights++;
						break;

					case refLightType_t::RL_DIRECTIONAL:
						numParallelLights++;
						break;

					default:
						break;
				}

				light++;
			}
		}

		numEntities++;
	}

	if ( ( numOmniLights + numProjLights + numParallelLights ) != s_worldData.numLights )
	{
		Sys::Drop( "counted %i lights and parsed %i lights", s_worldData.numLights, ( numOmniLights + numProjLights + numParallelLights ) );
	}

	Log::Debug("%i total entities parsed", numEntities );
	Log::Debug("%i total lights parsed", numOmniLights + numProjLights );
	Log::Debug("%i omni-directional lights parsed", numOmniLights );
	Log::Debug("%i projective lights parsed", numProjLights );
	Log::Debug("%i directional lights parsed", numParallelLights );
}

/*
=================
R_GetEntityToken
=================
*/
bool R_GetEntityToken( char *buffer, int size )
{
	const char *s;

	s = COM_Parse2( &s_worldData.entityParsePoint );
	Q_strncpyz( buffer, s, size );

	if ( !s_worldData.entityParsePoint || !s[ 0 ] )
	{
		s_worldData.entityParsePoint = s_worldData.entityString;
		return false;
	}
	else
	{
		return true;
	}
}

/*
=================
R_PrecacheInteraction
=================
*/
static void R_PrecacheInteraction( trRefLight_t *light, bspSurface_t *surface )
{
	interactionCache_t *iaCache;

	iaCache = (interactionCache_t*) ri.Hunk_Alloc( sizeof( *iaCache ), ha_pref::h_low );
	Com_AddToGrowList( &s_interactions, iaCache );

	// connect to interaction grid
	if ( !light->firstInteractionCache )
	{
		light->firstInteractionCache = iaCache;
	}

	if ( light->lastInteractionCache )
	{
		light->lastInteractionCache->next = iaCache;
	}

	light->lastInteractionCache = iaCache;

	iaCache->next = nullptr;
	iaCache->surface = surface;

	iaCache->redundant = false;
}

/*
================
R_PrecacheGenericSurfInteraction
================
*/
static bool R_PrecacheGenericSurfInteraction( srfGeneric_t *face, trRefLight_t *light )
{
	return BoundsIntersect( face->bounds[ 0 ], face->bounds[ 1 ], light->worldBounds[ 0 ], light->worldBounds[ 1 ] );
}

/*
======================
R_PrecacheInteractionSurface
======================
*/
static void R_PrecacheInteractionSurface( bspSurface_t *surf, trRefLight_t *light )
{
	bool intersects;

	if ( surf->lightCount == s_lightCount )
	{
		return; // already checked this surface
	}

	surf->lightCount = s_lightCount;

	// skip all surfaces that don't matter for lighting only pass
	if ( surf->shader->isSky || ( !surf->shader->interactLight && surf->shader->noShadows ) )
	{
		return;
	}

	switch ( *surf->data )
	{
		case surfaceType_t::SF_FACE:
		case surfaceType_t::SF_GRID:
		case surfaceType_t::SF_TRIANGLES:
			intersects = R_PrecacheGenericSurfInteraction( ( srfGeneric_t * ) surf->data, light );
			break;
		default:
			intersects = false;
			break;
	}

	if ( intersects )
	{
		R_PrecacheInteraction( light, surf );
	}
}

/*
================
R_RecursivePrecacheInteractionNode
================
*/
static void R_RecursivePrecacheInteractionNode( bspNode_t *node, trRefLight_t *light )
{
	int r;

	do
	{

		if ( node->contents != -1 )
		{
			break;
		}

		// node is just a decision point, so go down both sides
		// since we don't care about sort orders, just go positive to negative
		r = BoxOnPlaneSide( light->worldBounds[ 0 ], light->worldBounds[ 1 ], node->plane );

		switch ( r )
		{
			case 1:
				node = node->children[ 0 ];
				break;

			case 2:
				node = node->children[ 1 ];
				break;

			case 3:
			default:
				// recurse down the children, front side first
				R_RecursivePrecacheInteractionNode( node->children[ 0 ], light );

				// tail recurse
				node = node->children[ 1 ];
				break;
		}
	}
	while ( true );

	{
		// leaf node, so add mark surfaces
		int          c;
		bspSurface_t *surf, **mark;
		vec3_t       worldBounds[ 2 ];

		// add the individual surfaces
		mark = s_worldData.markSurfaces + node->firstMarkSurface;
		c = node->numMarkSurfaces;

		while ( c-- )
		{
			// the surface may have already been added if it
			// spans multiple leafs
			surf = *mark;
			R_PrecacheInteractionSurface( surf, light );
			mark++;
		}

		VectorCopy( node->mins, worldBounds[ 0 ] );
		VectorCopy( node->maxs, worldBounds[ 1 ] );

		if ( node->numMarkSurfaces > 0 && R_CullLightWorldBounds( light, worldBounds ) != cullResult_t::CULL_OUT )
		{
			link_t *l;

			l = ( link_t *)ri.Hunk_Alloc( sizeof( *l ), ha_pref::h_low );
			InitLink( l, node );

			InsertLink( l, &light->leafs );

			light->leafs.numElements++;
		}
	}
}

/*
=================
R_CreateInteractionVBO
=================
*/
static interactionVBO_t *R_CreateInteractionVBO( trRefLight_t *light )
{
	interactionVBO_t *iaVBO;

	iaVBO = (interactionVBO_t*) ri.Hunk_Alloc( sizeof( *iaVBO ), ha_pref::h_low );

	// connect to interaction grid
	if ( !light->firstInteractionVBO )
	{
		light->firstInteractionVBO = iaVBO;
	}

	if ( light->lastInteractionVBO )
	{
		light->lastInteractionVBO->next = iaVBO;
	}

	light->lastInteractionVBO = iaVBO;
	iaVBO->next = nullptr;

	return iaVBO;
}

/*
=================
InteractionCacheCompare
compare function for qsort()
=================
*/
static int InteractionCacheCompare( const void *a, const void *b )
{
	interactionCache_t *aa, *bb;

	aa = * ( interactionCache_t ** ) a;
	bb = * ( interactionCache_t ** ) b;

	// shader first
	if ( aa->surface->shader < bb->surface->shader )
	{
		return -1;
	}

	else if ( aa->surface->shader > bb->surface->shader )
	{
		return 1;
	}

	// then alphaTest
	if ( aa->surface->shader->alphaTest < bb->surface->shader->alphaTest )
	{
		return -1;
	}

	else if ( aa->surface->shader->alphaTest > bb->surface->shader->alphaTest )
	{
		return 1;
	}

	return 0;
}

static int UpdateLightTriangles( const srfVert_t *verts, int numTriangles, srfTriangle_t *triangles, shader_t *surfaceShader,
                                 trRefLight_t *light )
{
	int           i;
	srfTriangle_t *tri;
	int           numFacing;

	numFacing = 0;

	for ( i = 0, tri = triangles; i < numTriangles; i++, tri++ )
	{
		vec3_t pos[ 3 ];
		vec4_t triPlane;
		float  d;

		VectorCopy( verts[ tri->indexes[ 0 ] ].xyz, pos[ 0 ] );
		VectorCopy( verts[ tri->indexes[ 1 ] ].xyz, pos[ 1 ] );
		VectorCopy( verts[ tri->indexes[ 2 ] ].xyz, pos[ 2 ] );

		if ( PlaneFromPoints( triPlane, pos[ 0 ], pos[ 1 ], pos[ 2 ] ) )
		{

			if ( light->l.rlType == refLightType_t::RL_DIRECTIONAL )
			{
				vec3_t lightDirection;

				// light direction is from surface to light
				VectorCopy( tr.sunDirection, lightDirection );

				d = DotProduct( triPlane, lightDirection );

				tri->facingLight = surfaceShader->cullType == CT_TWO_SIDED || ( d > 0 && surfaceShader->cullType != CT_BACK_SIDED );
			}
			else
			{
				// check if light origin is behind triangle
				d = DotProduct( triPlane, light->origin ) - triPlane[ 3 ];

				tri->facingLight = surfaceShader->cullType == CT_TWO_SIDED || ( d > 0 && surfaceShader->cullType != CT_BACK_SIDED );
			}
		}
		else
		{
			tri->facingLight = true; // FIXME ?
		}

		if ( R_CullLightTriangle( light, pos ) == cullResult_t::CULL_OUT )
		{
			tri->facingLight = false;
		}

		if ( tri->facingLight )
		{
			numFacing++;
		}
	}

	return numFacing;
}

/*
===============
R_CreateVBOLightMeshes
===============
*/
static void R_CreateVBOLightMeshes( trRefLight_t *light )
{
	int                i, j, k, l;

	int                numVerts;

	int                numTriangles, numLitTriangles;
	srfTriangle_t      *tri;
	glIndex_t          *indexes;

	interactionVBO_t   *iaVBO;

	interactionCache_t *iaCache, *iaCache2;
	interactionCache_t **iaCachesSorted;
	int                numCaches;

	shader_t           *shader, *oldShader;

	bspSurface_t       *surface;

	srfVBOMesh_t       *vboSurf;
	vec3_t             bounds[ 2 ];

	if ( !r_vboLighting->integer )
	{
		return;
	}

	if ( !light->firstInteractionCache )
	{
		// this light has no interactions precached
		return;
	}

	// count number of interaction caches
	numCaches = 0;

	for ( iaCache = light->firstInteractionCache; iaCache; iaCache = iaCache->next )
	{
		if ( iaCache->redundant )
		{
			continue;
		}

		surface = iaCache->surface;

		if ( !surface->shader->interactLight )
		{
			continue;
		}

		if ( surface->shader->isPortal )
		{
			continue;
		}

		numCaches++;
	}

	// build interaction caches list
	iaCachesSorted = (interactionCache_t**) ri.Hunk_AllocateTempMemory( numCaches * sizeof( iaCachesSorted[ 0 ] ) );

	numCaches = 0;

	for ( iaCache = light->firstInteractionCache; iaCache; iaCache = iaCache->next )
	{
		if ( iaCache->redundant )
		{
			continue;
		}

		surface = iaCache->surface;

		if ( !surface->shader->interactLight )
		{
			continue;
		}

		if ( surface->shader->isPortal )
		{
			continue;
		}

		iaCachesSorted[ numCaches ] = iaCache;
		numCaches++;
	}

	// sort interaction caches by shader
	qsort( iaCachesSorted, numCaches, sizeof( *iaCachesSorted ), InteractionCacheCompare );

	// create a VBO for each shader
	shader = oldShader = nullptr;

	for ( k = 0; k < numCaches; k++ )
	{
		iaCache = iaCachesSorted[ k ];

		iaCache->mergedIntoVBO = true;

		shader = iaCache->surface->shader;

		if ( shader != oldShader )
		{
			oldShader = shader;

			// count vertices and indices
			numVerts = 0;
			numTriangles = 0;

			ClearBounds( bounds[ 0 ], bounds[ 1 ] );

			for ( l = k; l < numCaches; l++ )
			{
				iaCache2 = iaCachesSorted[ l ];

				surface = iaCache2->surface;

				if ( surface->shader != shader )
				{
					continue;
				}

				if ( *surface->data == surfaceType_t::SF_FACE )
				{
					srfSurfaceFace_t *srf = ( srfSurfaceFace_t * ) surface->data;

					if ( srf->numTriangles )
					{
						numLitTriangles = UpdateLightTriangles( s_worldData.verts, srf->numTriangles, s_worldData.triangles + srf->firstTriangle, surface->shader, light );

						if ( numLitTriangles )
						{
							BoundsAdd( bounds[ 0 ], bounds[ 1 ], srf->bounds[ 0 ], srf->bounds[ 1 ] );
						}

						numTriangles += numLitTriangles;
					}

					if ( srf->numVerts )
					{
						numVerts += srf->numVerts;
					}
				}
				else if ( *surface->data == surfaceType_t::SF_GRID )
				{
					srfGridMesh_t *srf = ( srfGridMesh_t * ) surface->data;

					if ( srf->numTriangles )
					{
						numLitTriangles = UpdateLightTriangles( s_worldData.verts, srf->numTriangles, s_worldData.triangles + srf->firstTriangle, surface->shader, light );

						if ( numLitTriangles )
						{
							BoundsAdd( bounds[ 0 ], bounds[ 1 ], srf->bounds[ 0 ], srf->bounds[ 1 ] );
						}

						numTriangles += numLitTriangles;
					}

					if ( srf->numVerts )
					{
						numVerts += srf->numVerts;
					}
				}
				else if ( *surface->data == surfaceType_t::SF_TRIANGLES )
				{
					srfTriangles_t *srf = ( srfTriangles_t * ) surface->data;

					if ( srf->numTriangles )
					{
						numLitTriangles = UpdateLightTriangles( s_worldData.verts, srf->numTriangles, s_worldData.triangles + srf->firstTriangle, surface->shader, light );

						if ( numLitTriangles )
						{
							BoundsAdd( bounds[ 0 ], bounds[ 1 ], srf->bounds[ 0 ], srf->bounds[ 1 ] );
						}

						numTriangles += numLitTriangles;
					}

					if ( srf->numVerts )
					{
						numVerts += srf->numVerts;
					}
				}
			}

			if ( !numVerts || !numTriangles )
			{
				continue;
			}

			// create surface
			vboSurf = (srfVBOMesh_t*) ri.Hunk_Alloc( sizeof( *vboSurf ), ha_pref::h_low );
			vboSurf->surfaceType = surfaceType_t::SF_VBO_MESH;
			vboSurf->numIndexes = numTriangles * 3;
			vboSurf->numVerts = numVerts;
			vboSurf->lightmapNum = -1;

			VectorCopy( bounds[ 0 ], vboSurf->bounds[ 0 ] );
			VectorCopy( bounds[ 1 ], vboSurf->bounds[ 1 ] );

			// create arrays
			indexes = (glIndex_t*) ri.Hunk_AllocateTempMemory( 3 * numTriangles * sizeof( glIndex_t ) );
			numTriangles = 0;

			// build triangle indices
			for ( l = k; l < numCaches; l++ )
			{
				iaCache2 = iaCachesSorted[ l ];

				surface = iaCache2->surface;

				if ( surface->shader != shader )
				{
					continue;
				}

				if ( *surface->data == surfaceType_t::SF_FACE )
				{
					srfSurfaceFace_t *srf = ( srfSurfaceFace_t * ) surface->data;

					for ( i = 0, tri = s_worldData.triangles + srf->firstTriangle; i < srf->numTriangles; i++, tri++ )
					{
						if ( tri->facingLight )
						{
							for ( j = 0; j < 3; j++ )
							{
								indexes[ 3 * numTriangles + j ] = tri->indexes[ j ];
							}

							numTriangles++;
						}
					}
				}
				else if ( *surface->data == surfaceType_t::SF_GRID )
				{
					srfGridMesh_t *srf = ( srfGridMesh_t * ) surface->data;

					for ( i = 0, tri = s_worldData.triangles + srf->firstTriangle; i < srf->numTriangles; i++, tri++ )
					{
						if ( tri->facingLight )
						{
							for ( j = 0; j < 3; j++ )
							{
								indexes[ 3 * numTriangles + j ] = tri->indexes[ j ];
							}

							numTriangles++;
						}
					}
				}
				else if ( *surface->data == surfaceType_t::SF_TRIANGLES )
				{
					srfTriangles_t *srf = ( srfTriangles_t * ) surface->data;

					for ( i = 0, tri = s_worldData.triangles + srf->firstTriangle; i < srf->numTriangles; i++, tri++ )
					{
						if ( tri->facingLight )
						{
							for ( j = 0; j < 3; j++ )
							{
								indexes[ 3 * numTriangles + j ] = tri->indexes[ j ];
							}

							numTriangles++;
						}
					}
				}
			}

			vboSurf->vbo = s_worldData.vbo;
			vboSurf->ibo = R_CreateStaticIBO( va( "staticLightMesh_IBO %i", c_vboLightSurfaces ), indexes, 3 * numTriangles );

			ri.Hunk_FreeTempMemory( indexes );

			// add everything needed to the light
			iaVBO = R_CreateInteractionVBO( light );
			iaVBO->shader = shader;
			iaVBO->vboLightMesh = vboSurf;

			c_vboLightSurfaces++;
		}
	}

	ri.Hunk_FreeTempMemory( iaCachesSorted );
}

/*
===============
R_CreateVBOShadowMeshes
===============
*/
static void R_CreateVBOShadowMeshes( trRefLight_t *light )
{
	int                i, j, k, l;

	int                numVerts;

	int                numTriangles, numLitTriangles;
	glIndex_t          *indexes;
	srfTriangle_t      *tri;

	interactionVBO_t   *iaVBO;

	interactionCache_t *iaCache, *iaCache2;
	interactionCache_t **iaCachesSorted;
	int                numCaches;

	shader_t           *shader, *oldShader;
	bool           alphaTest, oldAlphaTest;

	bspSurface_t       *surface;

	srfVBOMesh_t       *vboSurf;
	vec3_t             bounds[ 2 ];

	if ( !r_vboShadows->integer )
	{
		return;
	}

	if ( r_shadows->integer < Util::ordinal(shadowingMode_t::SHADOWING_ESM16))
	{
		return;
	}

	if ( !light->firstInteractionCache )
	{
		// this light has no interactions precached
		return;
	}

	if ( light->l.noShadows )
	{
		return;
	}

	switch ( light->l.rlType )
	{
		case refLightType_t::RL_OMNI:
			return;

		case refLightType_t::RL_DIRECTIONAL:
		case refLightType_t::RL_PROJ:
			break;

		default:
			return;
	}

	// count number of interaction caches
	numCaches = 0;

	for ( iaCache = light->firstInteractionCache; iaCache; iaCache = iaCache->next )
	{
		if ( iaCache->redundant )
		{
			continue;
		}

		surface = iaCache->surface;

		if ( !surface->shader->interactLight )
		{
			continue;
		}

		if ( surface->shader->isSky )
		{
			continue;
		}

		if ( surface->shader->noShadows )
		{
			continue;
		}

		if (surface->shader->sort > Util::ordinal(shaderSort_t::SS_OPAQUE))
		{
			continue;
		}

		if ( surface->shader->isPortal )
		{
			continue;
		}

		numCaches++;
	}

	// build interaction caches list
	iaCachesSorted = (interactionCache_t**) ri.Hunk_AllocateTempMemory( numCaches * sizeof( iaCachesSorted[ 0 ] ) );

	numCaches = 0;

	for ( iaCache = light->firstInteractionCache; iaCache; iaCache = iaCache->next )
	{
		if ( iaCache->redundant )
		{
			continue;
		}

		surface = iaCache->surface;

		if ( !surface->shader->interactLight )
		{
			continue;
		}

		if ( surface->shader->isSky )
		{
			continue;
		}

		if ( surface->shader->noShadows )
		{
			continue;
		}

		if ( surface->shader->sort > Util::ordinal(shaderSort_t::SS_OPAQUE) )
		{
			continue;
		}

		if ( surface->shader->isPortal )
		{
			continue;
		}

		iaCachesSorted[ numCaches ] = iaCache;
		numCaches++;
	}

	// sort interaction caches by shader
	qsort( iaCachesSorted, numCaches, sizeof( *iaCachesSorted ), InteractionCacheCompare );

	// create a VBO for each shader
	shader = oldShader = nullptr;
	oldAlphaTest = alphaTest = true;

	for ( k = 0; k < numCaches; k++ )
	{
		iaCache = iaCachesSorted[ k ];

		shader = iaCache->surface->shader;
		alphaTest = shader->alphaTest;

		iaCache->mergedIntoVBO = true;

		if ( alphaTest ? shader != oldShader : alphaTest != oldAlphaTest )
		{
			oldShader = shader;
			oldAlphaTest = alphaTest;

			// count vertices and indices
			numVerts = 0;
			numTriangles = 0;

			ClearBounds( bounds[ 0 ], bounds[ 1 ] );

			for ( l = k; l < numCaches; l++ )
			{
				iaCache2 = iaCachesSorted[ l ];

				surface = iaCache2->surface;

				if ( alphaTest )
				{
					if ( surface->shader != shader )
					{
						break;
					}
				}
				else
				{
					if ( surface->shader->alphaTest != alphaTest )
					{
						break;
					}
				}

				if ( *surface->data == surfaceType_t::SF_FACE )
				{
					srfSurfaceFace_t *srf = ( srfSurfaceFace_t * ) surface->data;

					if ( srf->numTriangles )
					{
						numLitTriangles = UpdateLightTriangles( s_worldData.verts, srf->numTriangles, s_worldData.triangles + srf->firstTriangle, surface->shader, light );

						if ( numLitTriangles )
						{
							BoundsAdd( bounds[ 0 ], bounds[ 1 ], srf->bounds[ 0 ], srf->bounds[ 1 ] );
						}

						numTriangles += numLitTriangles;
					}

					if ( srf->numVerts )
					{
						numVerts += srf->numVerts;
					}
				}
				else if ( *surface->data == surfaceType_t::SF_GRID )
				{
					srfGridMesh_t *srf = ( srfGridMesh_t * ) surface->data;

					if ( srf->numTriangles )
					{
						numLitTriangles = UpdateLightTriangles( s_worldData.verts, srf->numTriangles, s_worldData.triangles + srf->firstTriangle, surface->shader, light );

						if ( numLitTriangles )
						{
							BoundsAdd( bounds[ 0 ], bounds[ 1 ], srf->bounds[ 0 ], srf->bounds[ 1 ] );
						}

						numTriangles += numLitTriangles;
					}

					if ( srf->numVerts )
					{
						numVerts += srf->numVerts;
					}
				}
				else if ( *surface->data == surfaceType_t::SF_TRIANGLES )
				{
					srfTriangles_t *srf = ( srfTriangles_t * ) surface->data;

					if ( srf->numTriangles )
					{
						numLitTriangles = UpdateLightTriangles( s_worldData.verts, srf->numTriangles, s_worldData.triangles + srf->firstTriangle, surface->shader, light );

						if ( numLitTriangles )
						{
							BoundsAdd( bounds[ 0 ], bounds[ 1 ], srf->bounds[ 0 ], srf->bounds[ 1 ] );
						}

						numTriangles += numLitTriangles;
					}

					if ( srf->numVerts )
					{
						numVerts += srf->numVerts;
					}
				}
			}

			if ( !numVerts || !numTriangles )
			{
				continue;
			}

			// create surface
			vboSurf = (srfVBOMesh_t*) ri.Hunk_Alloc( sizeof( *vboSurf ), ha_pref::h_low );
			vboSurf->surfaceType = surfaceType_t::SF_VBO_MESH;
			vboSurf->numIndexes = numTriangles * 3;
			vboSurf->numVerts = numVerts;
			vboSurf->lightmapNum = -1;

			VectorCopy( bounds[ 0 ], vboSurf->bounds[ 0 ] );
			VectorCopy( bounds[ 1 ], vboSurf->bounds[ 1 ] );

			// create arrays
			indexes = (glIndex_t*) ri.Hunk_AllocateTempMemory( 3 * numTriangles * sizeof( glIndex_t ) );
			numTriangles = 0;

			// build triangle indices
			for ( l = k; l < numCaches; l++ )
			{
				iaCache2 = iaCachesSorted[ l ];

				surface = iaCache2->surface;

				if ( alphaTest )
				{
					if ( surface->shader != shader )
					{
						break;
					}
				}
				else
				{
					if ( surface->shader->alphaTest != alphaTest )
					{
						break;
					}
				}

				if ( *surface->data == surfaceType_t::SF_FACE )
				{
					srfSurfaceFace_t *srf = ( srfSurfaceFace_t * ) surface->data;

					for ( i = 0, tri = s_worldData.triangles + srf->firstTriangle; i < srf->numTriangles; i++, tri++ )
					{
						if ( tri->facingLight )
						{
							for ( j = 0; j < 3; j++ )
							{
								indexes[ 3 * numTriangles + j ] = tri->indexes[ j ];
							}

							numTriangles++;
						}
					}
				}
				else if ( *surface->data == surfaceType_t::SF_GRID )
				{
					srfGridMesh_t *srf = ( srfGridMesh_t * ) surface->data;

					for ( i = 0, tri = s_worldData.triangles + srf->firstTriangle; i < srf->numTriangles; i++, tri++ )
					{
						if ( tri->facingLight )
						{
							for ( j = 0; j < 3; j++ )
							{
								indexes[ 3 * numTriangles + j ] = tri->indexes[ j ];
							}

							numTriangles++;
						}
					}
				}
				else if ( *surface->data == surfaceType_t::SF_TRIANGLES )
				{
					srfTriangles_t *srf = ( srfTriangles_t * ) surface->data;

					for ( i = 0, tri = s_worldData.triangles + srf->firstTriangle; i < srf->numTriangles; i++, tri++ )
					{
						if ( tri->facingLight )
						{
							for ( j = 0; j < 3; j++ )
							{
								indexes[ 3 * numTriangles + j ] = tri->indexes[ j ];
							}

							numTriangles++;
						}
					}
				}
			}

			vboSurf->vbo = s_worldData.vbo;
			vboSurf->ibo = R_CreateStaticIBO( va( "staticShadowMesh_IBO %i", c_vboLightSurfaces ), indexes, 3 * numTriangles );

			ri.Hunk_FreeTempMemory( indexes );

			// add everything needed to the light
			iaVBO = R_CreateInteractionVBO( light );
			iaVBO->shader = shader;
			iaVBO->vboShadowMesh = vboSurf;

			c_vboShadowSurfaces++;
		}
	}

	ri.Hunk_FreeTempMemory( iaCachesSorted );
}

/*
===============
R_CreateVBOShadowCubeMeshes
===============
*/
static void R_CreateVBOShadowCubeMeshes( trRefLight_t *light )
{
	int                i, j, k, l;

	int                numVerts;

	int                numTriangles;
	glIndex_t          *indexes;
	srfTriangle_t      *tri;

	interactionVBO_t   *iaVBO;

	interactionCache_t *iaCache, *iaCache2;
	interactionCache_t **iaCachesSorted;
	int                numCaches;

	shader_t           *shader, *oldShader;
	bool           alphaTest, oldAlphaTest;
	int                cubeSide;

	bspSurface_t       *surface;

	srfVBOMesh_t       *vboSurf;

	if ( !r_vboShadows->integer )
	{
		return;
	}

	if ( r_shadows->integer < Util::ordinal(shadowingMode_t::SHADOWING_ESM16))
	{
		return;
	}

	if ( !light->firstInteractionCache )
	{
		// this light has no interactions precached
		return;
	}

	if ( light->l.noShadows )
	{
		return;
	}

	if ( light->l.rlType != refLightType_t::RL_OMNI )
	{
		return;
	}

	// count number of interaction caches
	numCaches = 0;

	for ( iaCache = light->firstInteractionCache; iaCache; iaCache = iaCache->next )
	{
		if ( iaCache->redundant )
		{
			continue;
		}

		surface = iaCache->surface;

		if ( !surface->shader->interactLight )
		{
			continue;
		}

		if ( surface->shader->isSky )
		{
			continue;
		}

		if ( surface->shader->noShadows )
		{
			continue;
		}

		if ( surface->shader->sort > Util::ordinal(shaderSort_t::SS_OPAQUE))
		{
			continue;
		}

		if ( surface->shader->isPortal )
		{
			continue;
		}

		numCaches++;
	}

	// build interaction caches list
	iaCachesSorted = (interactionCache_t**) ri.Hunk_AllocateTempMemory( numCaches * sizeof( iaCachesSorted[ 0 ] ) );

	numCaches = 0;

	for ( iaCache = light->firstInteractionCache; iaCache; iaCache = iaCache->next )
	{
		if ( iaCache->redundant )
		{
			continue;
		}

		surface = iaCache->surface;

		if ( !surface->shader->interactLight )
		{
			continue;
		}

		if ( surface->shader->isSky )
		{
			continue;
		}

		if ( surface->shader->noShadows )
		{
			continue;
		}

		if ( surface->shader->sort > Util::ordinal(shaderSort_t::SS_OPAQUE) )
		{
			continue;
		}

		if ( surface->shader->isPortal )
		{
			continue;
		}

		iaCachesSorted[ numCaches ] = iaCache;
		numCaches++;
	}

	// sort interaction caches by shader
	qsort( iaCachesSorted, numCaches, sizeof( *iaCachesSorted ), InteractionCacheCompare );

	// create a VBO for each shader
	for ( cubeSide = 0; cubeSide < 6; cubeSide++ )
	{
		shader = oldShader = nullptr;
		oldAlphaTest = alphaTest = true;

		for ( k = 0; k < numCaches; k++ )
		{
			iaCache = iaCachesSorted[ k ];
			shader = iaCache->surface->shader;
			alphaTest = shader->alphaTest;

			iaCache->mergedIntoVBO = true;

			if ( alphaTest ? shader != oldShader : alphaTest != oldAlphaTest )
			{
				oldShader = shader;
				oldAlphaTest = alphaTest;

				// count vertices and indices
				numVerts = 0;
				numTriangles = 0;

				for ( l = k; l < numCaches; l++ )
				{
					iaCache2 = iaCachesSorted[ l ];

					surface = iaCache2->surface;

					if ( alphaTest )
					{
						if ( surface->shader != shader )
						{
							break;
						}
					}
					else
					{
						if ( surface->shader->alphaTest != alphaTest )
						{
							break;
						}
					}

					if ( !( iaCache2->cubeSideBits & ( 1 << cubeSide ) ) )
					{
						continue;
					}

					if ( *surface->data == surfaceType_t::SF_FACE )
					{
						srfSurfaceFace_t *srf = ( srfSurfaceFace_t * ) surface->data;

						if ( srf->numTriangles )
						{
							numTriangles +=
							  UpdateLightTriangles( s_worldData.verts, srf->numTriangles,
							                        s_worldData.triangles + srf->firstTriangle, surface->shader, light );
						}

						if ( srf->numVerts )
						{
							numVerts += srf->numVerts;
						}
					}
					else if ( *surface->data == surfaceType_t::SF_GRID )
					{
						srfGridMesh_t *srf = ( srfGridMesh_t * ) surface->data;

						if ( srf->numTriangles )
						{
							numTriangles +=
							  UpdateLightTriangles( s_worldData.verts, srf->numTriangles,
							                        s_worldData.triangles + srf->firstTriangle, surface->shader, light );
						}

						if ( srf->numVerts )
						{
							numVerts += srf->numVerts;
						}
					}
					else if ( *surface->data == surfaceType_t::SF_TRIANGLES )
					{
						srfTriangles_t *srf = ( srfTriangles_t * ) surface->data;

						if ( srf->numTriangles )
						{
							numTriangles +=
							  UpdateLightTriangles( s_worldData.verts, srf->numTriangles,
							                        s_worldData.triangles + srf->firstTriangle, surface->shader, light );
						}

						if ( srf->numVerts )
						{
							numVerts += srf->numVerts;
						}
					}
				}

				if ( !numVerts || !numTriangles )
				{
					continue;
				}

				// create surface
				vboSurf = (srfVBOMesh_t*) ri.Hunk_Alloc( sizeof( *vboSurf ), ha_pref::h_low );
				vboSurf->surfaceType = surfaceType_t::SF_VBO_MESH;
				vboSurf->numIndexes = numTriangles * 3;
				vboSurf->numVerts = numVerts;
				vboSurf->lightmapNum = -1;
				ZeroBounds( vboSurf->bounds[ 0 ], vboSurf->bounds[ 1 ] );

				// create arrays
				indexes = (glIndex_t*) ri.Hunk_AllocateTempMemory( 3 * numTriangles * sizeof( glIndex_t ) );
				numTriangles = 0;

				// build triangle indices
				for ( l = k; l < numCaches; l++ )
				{
					iaCache2 = iaCachesSorted[ l ];

					surface = iaCache2->surface;

					if ( alphaTest )
					{
						if ( surface->shader != shader )
						{
							break;
						}
					}
					else
					{
						if ( surface->shader->alphaTest != alphaTest )
						{
							break;
						}
					}

					if ( !( iaCache2->cubeSideBits & ( 1 << cubeSide ) ) )
					{
						continue;
					}

					if ( *surface->data == surfaceType_t::SF_FACE )
					{
						srfSurfaceFace_t *srf = ( srfSurfaceFace_t * ) surface->data;

						for ( i = 0, tri = s_worldData.triangles + srf->firstTriangle; i < srf->numTriangles; i++, tri++ )
						{
							if ( tri->facingLight )
							{
								for ( j = 0; j < 3; j++ )
								{
									indexes[ 3 * numTriangles + j ] = tri->indexes[ j ];
								}

								numTriangles++;
							}
						}
					}
					else if ( *surface->data == surfaceType_t::SF_GRID )
					{
						srfGridMesh_t *srf = ( srfGridMesh_t * ) surface->data;

						for ( i = 0, tri = s_worldData.triangles + srf->firstTriangle; i < srf->numTriangles; i++, tri++ )
						{
							if ( tri->facingLight )
							{
								for ( j = 0; j < 3; j++ )
								{
									indexes[ 3 * numTriangles + j ] = tri->indexes[ j ];
								}

								numTriangles++;
							}
						}
					}
					else if ( *surface->data == surfaceType_t::SF_TRIANGLES )
					{
						srfTriangles_t *srf = ( srfTriangles_t * ) surface->data;

						for ( i = 0, tri = s_worldData.triangles + srf->firstTriangle; i < srf->numTriangles; i++, tri++ )
						{
							if ( tri->facingLight )
							{
								for ( j = 0; j < 3; j++ )
								{
									indexes[ 3 * numTriangles + j ] = tri->indexes[ j ];
								}

								numTriangles++;
							}
						}
					}
				}

				vboSurf->vbo = s_worldData.vbo;
				vboSurf->ibo = R_CreateStaticIBO( va( "staticShadowPyramidMesh_IBO %i", c_vboShadowSurfaces ), indexes, numTriangles );

				ri.Hunk_FreeTempMemory( indexes );

				// add everything needed to the light
				iaVBO = R_CreateInteractionVBO( light );
				iaVBO->cubeSideBits = ( 1 << cubeSide );
				iaVBO->shader = shader;
				iaVBO->vboShadowMesh = vboSurf;

				c_vboShadowSurfaces++;
			}
		}
	}

	ri.Hunk_FreeTempMemory( iaCachesSorted );
}

static void R_CalcInteractionCubeSideBits( trRefLight_t *light )
{
	interactionCache_t *iaCache;
	bspSurface_t       *surface;
	vec3_t             localBounds[ 2 ];

	if ( r_shadows->integer <= Util::ordinal(shadowingMode_t::SHADOWING_BLOB))
	{
		return;
	}

	if ( !light->firstInteractionCache )
	{
		// this light has no interactions precached
		return;
	}

	if ( light->l.noShadows )
	{
		// actually noShadows lights are quite bad concerning this optimization
		return;
	}

	if ( light->l.rlType != refLightType_t::RL_OMNI )
	{
		return;
	}

	{
		for ( iaCache = light->firstInteractionCache; iaCache; iaCache = iaCache->next )
		{
			surface = iaCache->surface;

			if ( *surface->data == surfaceType_t::SF_FACE || *surface->data == surfaceType_t::SF_GRID || *surface->data == surfaceType_t::SF_TRIANGLES )
			{
				srfGeneric_t *gen;

				gen = ( srfGeneric_t * ) surface->data;

				VectorCopy( gen->bounds[ 0 ], localBounds[ 0 ] );
				VectorCopy( gen->bounds[ 1 ], localBounds[ 1 ] );
			}
			else
			{
				iaCache->cubeSideBits = CUBESIDE_CLIPALL;
				continue;
			}

			light->shadowLOD = 0; // important for R_CalcLightCubeSideBits
			iaCache->cubeSideBits = R_CalcLightCubeSideBits( light, localBounds );
		}
	}
}

/*
=============
R_PrecacheInteractions
=============
*/
void R_PrecacheInteractions()
{
	int          i;
	trRefLight_t *light;
	bspSurface_t *surface;
	int          startTime, endTime;

	startTime = ri.Milliseconds();

	// reset surfaces' viewCount
	s_lightCount = 0;

	for ( i = 0, surface = s_worldData.surfaces; i < s_worldData.numSurfaces; i++, surface++ )
	{
		surface->lightCount = -1;
	}

	Com_InitGrowList( &s_interactions, 100 );

	c_redundantInteractions = 0;
	c_vboWorldSurfaces = 0;
	c_vboLightSurfaces = 0;
	c_vboShadowSurfaces = 0;

	Log::Debug("...precaching %i lights", s_worldData.numLights );

	for ( i = 0; i < s_worldData.numLights; i++ )
	{
		light = &s_worldData.lights[ i ];

		if ( tr.worldLightMapping && !light->noRadiosity )
		{
			continue;
		}

		// set up light transform matrix
		MatrixSetupTransformFromQuat( light->transformMatrix, light->l.rotation, light->l.origin );

		// set up light origin for lighting and shadowing
		R_SetupLightOrigin( light );

		// set up model to light view matrix
		R_SetupLightView( light );

		// set up projection
		R_SetupLightProjection( light );

		// calc local bounds for culling
		R_SetupLightLocalBounds( light );

		// setup world bounds for intersection tests
		R_SetupLightWorldBounds( light );

		// setup frustum planes for intersection tests
		R_SetupLightFrustum( light );

		// setup interactions
		light->firstInteractionCache = nullptr;
		light->lastInteractionCache = nullptr;

		light->firstInteractionVBO = nullptr;
		light->lastInteractionVBO = nullptr;

		// perform culling and add all the potentially visible surfaces
		s_lightCount++;
		QueueInit( &light->leafs );
		R_RecursivePrecacheInteractionNode( s_worldData.nodes, light );

		// create a static VBO surface for each light geometry batch
		R_CreateVBOLightMeshes( light );

		// create a static VBO surface for each shadow geometry batch
		R_CreateVBOShadowMeshes( light );

		// calculate pyramid bits for each interaction in omni-directional lights
		R_CalcInteractionCubeSideBits( light );

		// create a static VBO surface for each light geometry batch inside a cubemap pyramid
		R_CreateVBOShadowCubeMeshes( light );
	}

	// move interactions grow list to hunk
	s_worldData.numInteractions = s_interactions.currentElements;
	s_worldData.interactions = (interactionCache_t**) ri.Hunk_Alloc( s_worldData.numInteractions * sizeof( *s_worldData.interactions ), ha_pref::h_low );

	for ( i = 0; i < s_worldData.numInteractions; i++ )
	{
		s_worldData.interactions[ i ] = ( interactionCache_t * ) Com_GrowListElement( &s_interactions, i );
	}

	Com_DestroyGrowList( &s_interactions );

	Log::Debug("%i interactions precached", s_worldData.numInteractions );
	Log::Debug("%i interactions were hidden in shadows", c_redundantInteractions );

	if ( r_shadows->integer >= Util::ordinal(shadowingMode_t::SHADOWING_ESM16))
	{
		// only interesting for omni-directional shadow mapping
		Log::Debug("%i omni pyramid tests", tr.pc.c_pyramidTests );
		Log::Debug("%i omni pyramid surfaces visible", tr.pc.c_pyramid_cull_ent_in );
		Log::Debug("%i omni pyramid surfaces clipped", tr.pc.c_pyramid_cull_ent_clip );
		Log::Debug("%i omni pyramid surfaces culled", tr.pc.c_pyramid_cull_ent_out );
	}

	endTime = ri.Milliseconds();

	Log::Debug("lights precaching time = %5.2f seconds", ( endTime - startTime ) / 1000.0 );
}

static const int HASHTABLE_SIZE = 7919; // 32749 // 2039    /* prime, use % */
#define HASH_USE_EPSILON

#ifdef HASH_USE_EPSILON
#define HASH_XYZ_EPSILON                 0.01f
#define HASH_XYZ_EPSILONSPACE_MULTIPLIER 1.f / HASH_XYZ_EPSILON
#endif

unsigned int VertexCoordGenerateHash( const vec3_t xyz )
{
	unsigned int hash = 0;

#ifndef HASH_USE_EPSILON
	hash += ~( * ( ( unsigned int * ) &xyz[ 0 ] ) << 15 );
	hash ^= ( * ( ( unsigned int * ) &xyz[ 0 ] ) >> 10 );
	hash += ( * ( ( unsigned int * ) &xyz[ 1 ] ) << 3 );
	hash ^= ( * ( ( unsigned int * ) &xyz[ 1 ] ) >> 6 );
	hash += ~( * ( ( unsigned int * ) &xyz[ 2 ] ) << 11 );
	hash ^= ( * ( ( unsigned int * ) &xyz[ 2 ] ) >> 16 );
#else
	vec3_t xyz_epsilonspace;

	VectorScale( xyz, HASH_XYZ_EPSILONSPACE_MULTIPLIER, xyz_epsilonspace );
	xyz_epsilonspace[ 0 ] = floor( xyz_epsilonspace[ 0 ] );
	xyz_epsilonspace[ 1 ] = floor( xyz_epsilonspace[ 1 ] );
	xyz_epsilonspace[ 2 ] = floor( xyz_epsilonspace[ 2 ] );

	hash += ~( * ( ( unsigned int * ) &xyz_epsilonspace[ 0 ] ) << 15 );
	hash ^= ( * ( ( unsigned int * ) &xyz_epsilonspace[ 0 ] ) >> 10 );
	hash += ( * ( ( unsigned int * ) &xyz_epsilonspace[ 1 ] ) << 3 );
	hash ^= ( * ( ( unsigned int * ) &xyz_epsilonspace[ 1 ] ) >> 6 );
	hash += ~( * ( ( unsigned int * ) &xyz_epsilonspace[ 2 ] ) << 11 );
	hash ^= ( * ( ( unsigned int * ) &xyz_epsilonspace[ 2 ] ) >> 16 );

#endif

	hash = hash % ( HASHTABLE_SIZE );
	return hash;
}

vertexHash_t **NewVertexHashTable()
{
	vertexHash_t **hashTable = (vertexHash_t**) malloc( HASHTABLE_SIZE * sizeof( vertexHash_t * ) );

	memset( hashTable, 0, HASHTABLE_SIZE * sizeof( vertexHash_t * ) );

	return hashTable;
}

void FreeVertexHashTable( vertexHash_t **hashTable )
{
	int          i;
	vertexHash_t *vertexHash;
	vertexHash_t *nextVertexHash;

	if ( hashTable == nullptr )
	{
		return;
	}

	for ( i = 0; i < HASHTABLE_SIZE; i++ )
	{
		if ( hashTable[ i ] )
		{
			nextVertexHash = nullptr;

			for ( vertexHash = hashTable[ i ]; vertexHash; vertexHash = nextVertexHash )
			{
				nextVertexHash = vertexHash->next;

				free( vertexHash );
			}
		}
	}

	free( hashTable );
}

vertexHash_t *FindVertexInHashTable( vertexHash_t **hashTable, const vec3_t xyz, float distance )
{
	unsigned int hash;
	vertexHash_t *vertexHash;

	if ( hashTable == nullptr || xyz == nullptr )
	{
		return nullptr;
	}

	hash = VertexCoordGenerateHash( xyz );

	for ( vertexHash = hashTable[ hash ]; vertexHash; vertexHash = vertexHash->next )
	{
#ifndef HASH_USE_EPSILON

		if ( ( vertexHash->vcd.xyz[ 0 ] != xyz[ 0 ] || vertexHash->vcd.xyz[ 1 ] != xyz[ 1 ] ||
		       vertexHash->vcd.xyz[ 2 ] != xyz[ 2 ] ) )
		{
			continue;
		}

#elif 1

		if ( Distance( xyz, vertexHash->xyz ) > distance )
		{
			continue;
		}

#else

		if ( ( fabsf( xyz[ 0 ] - vertexHash->vcd.xyz[ 0 ] ) ) > HASH_XYZ_EPSILON ||
		     ( fabsf( xyz[ 1 ] - vertexHash->vcd.xyz[ 1 ] ) ) > HASH_XYZ_EPSILON ||
		     ( fabsf( xyz[ 2 ] - vertexHash->vcd.xyz[ 2 ] ) ) > HASH_XYZ_EPSILON )
		{
			continue;
		}

#endif
		return vertexHash;
	}

	return nullptr;
}

vertexHash_t *AddVertexToHashTable( vertexHash_t **hashTable, vec3_t xyz, void *data )
{
	unsigned int hash;
	vertexHash_t *vertexHash;

	if ( hashTable == nullptr || xyz == nullptr )
	{
		return nullptr;
	}

	vertexHash = (vertexHash_t*) malloc( sizeof( vertexHash_t ) );

	if ( !vertexHash )
	{
		return nullptr;
	}

	hash = VertexCoordGenerateHash( xyz );

	VectorCopy( xyz, vertexHash->xyz );
	vertexHash->data = data;

	// link into table
	vertexHash->next = hashTable[ hash ];
	hashTable[ hash ] = vertexHash;

	return vertexHash;
}

void GL_BindNearestCubeMap( const vec3_t xyz )
{
	float          distance, maxDistance;
	cubemapProbe_t *cubeProbe;
	unsigned int   hash;
	vertexHash_t   *vertexHash;

	tr.autoCubeImage = tr.whiteCubeImage;

	if ( !r_reflectionMapping->integer )
	{
		return;
	}

	if ( tr.cubeHashTable == nullptr || xyz == nullptr )
	{
		return;
	}

	maxDistance = 9999999.0f;

	hash = VertexCoordGenerateHash( xyz );

	for ( vertexHash = tr.cubeHashTable[ hash ]; vertexHash; vertexHash = vertexHash->next )
	{
		cubeProbe = (cubemapProbe_t*) vertexHash->data;

		distance = Distance( cubeProbe->origin, xyz );

		if ( distance < maxDistance )
		{
			tr.autoCubeImage = cubeProbe->cubemap;
			maxDistance = distance;
		}
	}

	GL_Bind( tr.autoCubeImage );
}

void R_FindTwoNearestCubeMaps( const vec3_t position, cubemapProbe_t **cubeProbeNearest, cubemapProbe_t **cubeProbeSecondNearest )
{
	int            j;
	float          distance, maxDistance, maxDistance2;
	cubemapProbe_t *cubeProbe;
	unsigned int   hash;
	vertexHash_t   *vertexHash;

	GLimp_LogComment( "--- R_FindTwoNearestCubeMaps ---\n" );

	*cubeProbeNearest = nullptr;
	*cubeProbeSecondNearest = nullptr;

	if ( tr.cubeHashTable == nullptr || position == nullptr )
	{
		return;
	}

	hash = VertexCoordGenerateHash( position );
	maxDistance = maxDistance2 = 9999999.0f;

	for ( j = 0, vertexHash = tr.cubeHashTable[ hash ]; vertexHash; vertexHash = vertexHash->next, j++ )
	{
		cubeProbe = (cubemapProbe_t*) vertexHash->data;
		distance = Distance( cubeProbe->origin, position );

		if ( distance < maxDistance )
		{
			*cubeProbeSecondNearest = *cubeProbeNearest;
			maxDistance2 = maxDistance;

			*cubeProbeNearest = cubeProbe;
			maxDistance = distance;
		}
		else if ( distance < maxDistance2 && distance > maxDistance )
		{
			*cubeProbeSecondNearest = cubeProbe;
			maxDistance2 = distance;
		}
	}
}

void R_BuildCubeMaps()
{
	int            i, j;
	int            ii, jj;
	refdef_t       rf;
	bool       flipx;
	bool       flipy;
	int            x, y, xy, xy2;

	cubemapProbe_t *cubeProbe;
	byte           temp[ REF_CUBEMAP_SIZE * REF_CUBEMAP_SIZE * 4 ];
	byte           *dest;

	int    startTime, endTime;
	size_t tics = 0;
	int nextTicCount = 0;

	// Early abort if a BSP is not loaded yet since
	// the buildcubemaps command can be called from
	// everywhere including the main menu.
	if ( tr.world == nullptr )
	{
		return;
	}

	startTime = ri.Milliseconds();

	memset( &rf, 0, sizeof( refdef_t ) );

	for ( i = 0; i < 6; i++ )
	{
		tr.cubeTemp[ i ] = (byte*) ri.Z_Malloc( REF_CUBEMAP_SIZE * REF_CUBEMAP_SIZE * 4 );
	}

	// calculate origins for our probes
	Com_InitGrowList( &tr.cubeProbes, 4000 );
	tr.cubeHashTable = NewVertexHashTable();

	{
		bspNode_t *node;

		for ( i = 0; i < tr.world->numnodes; i++ )
		{
			node = &tr.world->nodes[ i ];

			// check to see if this is a shit location
			if ( node->contents == CONTENTS_NODE )
			{
				continue;
			}

			if ( node->area == -1 )
			{
				// location is in the void
				continue;
			}

			vec3_t origin;
			VectorAdd( node->maxs, node->mins, origin );
			VectorScale( origin, 0.5, origin );

			if ( FindVertexInHashTable( tr.cubeHashTable, origin, 256 ) == nullptr )
			{
				cubeProbe = (cubemapProbe_t*) ri.Hunk_Alloc( sizeof( *cubeProbe ), ha_pref::h_high );
				Com_AddToGrowList( &tr.cubeProbes, cubeProbe );

				VectorCopy( origin, cubeProbe->origin );

				AddVertexToHashTable( tr.cubeHashTable, cubeProbe->origin, cubeProbe );
			}
		}
	}

	// if we can't find one, fake one
	if ( tr.cubeProbes.currentElements == 0 )
	{
		cubeProbe = (cubemapProbe_t*) ri.Hunk_Alloc( sizeof( *cubeProbe ), ha_pref::h_low );
		Com_AddToGrowList( &tr.cubeProbes, cubeProbe );

		VectorClear( cubeProbe->origin );
	}

	Log::Notice("...pre-rendering %d cubemaps", tr.cubeProbes.currentElements );
	Log::Notice("0%%  10   20   30   40   50   60   70   80   90   100%%" );
	Log::Notice("|----|----|----|----|----|----|----|----|----|----|" );

	for ( j = 0; j < tr.cubeProbes.currentElements; j++ )
	{
		cubeProbe = (cubemapProbe_t*) Com_GrowListElement( &tr.cubeProbes, j );

		//Log::Notice("rendering cubemap at (%i %i %i)", (int)cubeProbe->origin[0], (int)cubeProbe->origin[1],
		//      (int)cubeProbe->origin[2]);

		if ( ( j + 1 ) >= nextTicCount )
		{
			size_t ticsNeeded = ( size_t )( ( ( double )( j + 1 ) / tr.cubeProbes.currentElements ) * 50.0 );

			do
			{
				Log::Notice("*");
				Cmd::ExecuteCommand("updatescreen");
			}
			while ( ++tics < ticsNeeded );

			nextTicCount = ( size_t )( ( tics / 50.0 ) * tr.cubeProbes.currentElements );

			if ( ( j + 1 ) == tr.cubeProbes.currentElements )
			{
				if ( tics < 51 )
				{
					Log::Notice("*");
				}

				Log::Notice("");
			}
		}

		VectorCopy( cubeProbe->origin, rf.vieworg );

		AxisClear( rf.viewaxis );

		rf.fov_x = 90;
		rf.fov_y = 90;
		rf.x = 0;
		rf.y = 0;
		rf.width = REF_CUBEMAP_SIZE;
		rf.height = REF_CUBEMAP_SIZE;
		rf.time = 0;

		rf.rdflags = RDF_NOCUBEMAP | RDF_NOBLOOM;

		for ( i = 0; i < 6; i++ )
		{
			flipx = false;
			flipy = false;

			switch ( i )
			{
				case 0:
					{
						//X+
						rf.viewaxis[ 0 ][ 0 ] = 1;
						rf.viewaxis[ 0 ][ 1 ] = 0;
						rf.viewaxis[ 0 ][ 2 ] = 0;

						rf.viewaxis[ 1 ][ 0 ] = 0;
						rf.viewaxis[ 1 ][ 1 ] = 0;
						rf.viewaxis[ 1 ][ 2 ] = 1;

						CrossProduct( rf.viewaxis[ 0 ], rf.viewaxis[ 1 ], rf.viewaxis[ 2 ] );
						//flipx=true;
						break;
					}

				case 1:
					{
						//X-
						rf.viewaxis[ 0 ][ 0 ] = -1;
						rf.viewaxis[ 0 ][ 1 ] = 0;
						rf.viewaxis[ 0 ][ 2 ] = 0;

						rf.viewaxis[ 1 ][ 0 ] = 0;
						rf.viewaxis[ 1 ][ 1 ] = 0;
						rf.viewaxis[ 1 ][ 2 ] = -1;

						CrossProduct( rf.viewaxis[ 0 ], rf.viewaxis[ 1 ], rf.viewaxis[ 2 ] );
						//flipx=true;
						break;
					}

				case 2:
					{
						//Y+
						rf.viewaxis[ 0 ][ 0 ] = 0;
						rf.viewaxis[ 0 ][ 1 ] = 1;
						rf.viewaxis[ 0 ][ 2 ] = 0;

						rf.viewaxis[ 1 ][ 0 ] = -1;
						rf.viewaxis[ 1 ][ 1 ] = 0;
						rf.viewaxis[ 1 ][ 2 ] = 0;

						CrossProduct( rf.viewaxis[ 0 ], rf.viewaxis[ 1 ], rf.viewaxis[ 2 ] );
						//flipx=true;
						break;
					}

				case 3:
					{
						//Y-
						rf.viewaxis[ 0 ][ 0 ] = 0;
						rf.viewaxis[ 0 ][ 1 ] = -1;
						rf.viewaxis[ 0 ][ 2 ] = 0;

						rf.viewaxis[ 1 ][ 0 ] = -1; //-1
						rf.viewaxis[ 1 ][ 1 ] = 0;
						rf.viewaxis[ 1 ][ 2 ] = 0;

						CrossProduct( rf.viewaxis[ 0 ], rf.viewaxis[ 1 ], rf.viewaxis[ 2 ] );
						//flipx=true;
						break;
					}

				case 4:
					{
						//Z+
						rf.viewaxis[ 0 ][ 0 ] = 0;
						rf.viewaxis[ 0 ][ 1 ] = 0;
						rf.viewaxis[ 0 ][ 2 ] = 1;

						rf.viewaxis[ 1 ][ 0 ] = -1;
						rf.viewaxis[ 1 ][ 1 ] = 0;
						rf.viewaxis[ 1 ][ 2 ] = 0;

						CrossProduct( rf.viewaxis[ 0 ], rf.viewaxis[ 1 ], rf.viewaxis[ 2 ] );
						//  flipx=true;
						break;
					}

				case 5:
					{
						//Z-
						rf.viewaxis[ 0 ][ 0 ] = 0;
						rf.viewaxis[ 0 ][ 1 ] = 0;
						rf.viewaxis[ 0 ][ 2 ] = -1;

						rf.viewaxis[ 1 ][ 0 ] = 1;
						rf.viewaxis[ 1 ][ 1 ] = 0;
						rf.viewaxis[ 1 ][ 2 ] = 0;

						CrossProduct( rf.viewaxis[ 0 ], rf.viewaxis[ 1 ], rf.viewaxis[ 2 ] );
						//flipx=true;
						break;
					}
			}

			tr.refdef.pixelTarget = tr.cubeTemp[ i ];
			memset( tr.cubeTemp[ i ], 255, REF_CUBEMAP_SIZE * REF_CUBEMAP_SIZE * 4 );
			tr.refdef.pixelTargetWidth = REF_CUBEMAP_SIZE;
			tr.refdef.pixelTargetHeight = REF_CUBEMAP_SIZE;

			RE_BeginFrame();
			RE_RenderScene( &rf );
			RE_EndFrame( &ii, &jj );

			if ( flipx )
			{
				dest = tr.cubeTemp[ i ];
				memcpy( temp, dest, REF_CUBEMAP_SIZE * REF_CUBEMAP_SIZE * 4 );

				for ( y = 0; y < REF_CUBEMAP_SIZE; y++ )
				{
					for ( x = 0; x < REF_CUBEMAP_SIZE; x++ )
					{
						xy = ( ( y * REF_CUBEMAP_SIZE ) + x ) * 4;
						xy2 = ( ( y * REF_CUBEMAP_SIZE ) + ( ( REF_CUBEMAP_SIZE - 1 ) - x ) ) * 4;
						dest[ xy2 + 0 ] = temp[ xy + 0 ];
						dest[ xy2 + 1 ] = temp[ xy + 1 ];
						dest[ xy2 + 2 ] = temp[ xy + 2 ];
						dest[ xy2 + 3 ] = temp[ xy + 3 ];
					}
				}
			}

			if ( flipy )
			{
				dest = tr.cubeTemp[ i ];
				memcpy( temp, dest, REF_CUBEMAP_SIZE * REF_CUBEMAP_SIZE * 4 );

				for ( y = 0; y < REF_CUBEMAP_SIZE; y++ )
				{
					for ( x = 0; x < REF_CUBEMAP_SIZE; x++ )
					{
						xy = ( ( y * REF_CUBEMAP_SIZE ) + x ) * 4;
						xy2 = ( ( ( ( REF_CUBEMAP_SIZE - 1 ) - y ) * REF_CUBEMAP_SIZE ) + x ) * 4;
						dest[ xy2 + 0 ] = temp[ xy + 0 ];
						dest[ xy2 + 1 ] = temp[ xy + 1 ];
						dest[ xy2 + 2 ] = temp[ xy + 2 ];
						dest[ xy2 + 3 ] = temp[ xy + 3 ];
					}
				}
			}

			// encode the pixel intensity into the alpha channel, saves work in the shader
			byte r, g, b, best;

			dest = tr.cubeTemp[ i ];

			for ( y = 0; y < REF_CUBEMAP_SIZE; y++ )
			{
				for ( x = 0; x < REF_CUBEMAP_SIZE; x++ )
				{
					xy = ( ( y * REF_CUBEMAP_SIZE ) + x ) * 4;

					r = dest[ xy + 0 ];
					g = dest[ xy + 1 ];
					b = dest[ xy + 2 ];

					if ( ( r > g ) && ( r > b ) )
					{
						best = r;
					}
					else if ( ( g > r ) && ( g > b ) )
					{
						best = g;
					}
					else
					{
						best = b;
					}

					dest[ xy + 3 ] = best;
				}
			}
		}

		// build the cubemap
		cubeProbe->cubemap = R_AllocImage( va( "_autoCube%d", j ), false );

		if ( !cubeProbe->cubemap )
		{
			return;
		}

		cubeProbe->cubemap->type = GL_TEXTURE_CUBE_MAP;

		cubeProbe->cubemap->width = REF_CUBEMAP_SIZE;
		cubeProbe->cubemap->height = REF_CUBEMAP_SIZE;

		cubeProbe->cubemap->bits = IF_NOPICMIP;
		cubeProbe->cubemap->filterType = filterType_t::FT_LINEAR;
		cubeProbe->cubemap->wrapType = wrapTypeEnum_t::WT_EDGE_CLAMP;

		imageParams_t imageParams = {};

		R_UploadImage( ( const byte ** ) tr.cubeTemp, 6, 1, cubeProbe->cubemap, imageParams );
	}

	Log::Notice("");

	// turn pixel targets off
	tr.refdef.pixelTarget = nullptr;

	// assign the surfs a cubemap
	endTime = ri.Milliseconds();
	Log::Notice("cubemap probes pre-rendering time of %i cubes = %5.2f seconds", tr.cubeProbes.currentElements,
	           ( endTime - startTime ) / 1000.0 );
}

/*
=================
RE_LoadWorldMap

Called directly from cgame
=================
*/
void RE_LoadWorldMap( const char *name )
{
	int       i;
	dheader_t *header;
	byte      *startMarker;

	if ( tr.worldMapLoaded )
	{
		Sys::Drop( "ERROR: attempted to redundantly load world map" );
	}

	Log::Debug("----- RE_LoadWorldMap( %s ) -----", name );

	// set default sun direction to be used if it isn't
	// overridden by a shader
	tr.sunDirection[ 0 ] = 0.45f;
	tr.sunDirection[ 1 ] = 0.3f;
	tr.sunDirection[ 2 ] = 0.9f;

	VectorNormalize( tr.sunDirection );

	// set default ambient color
	tr.worldEntity.ambientLight[ 0 ] = r_forceAmbient->value;
	tr.worldEntity.ambientLight[ 1 ] = r_forceAmbient->value;
	tr.worldEntity.ambientLight[ 2 ] = r_forceAmbient->value;

	tr.worldMapLoaded = true;

	// load it
	std::error_code err;
	std::string buffer = FS::PakPath::ReadFile( name, err );

	if ( err )
	{
		Sys::Drop( "RE_LoadWorldMap: %s not found", name );
	}

	// clear tr.world so if the level fails to load, the next
	// try will not look at the partially loaded version
	tr.world = nullptr;

	// tr.worldDeluxeMapping will be set by R_LoadLightmaps()
	tr.worldLightMapping = false;
	// tr.worldDeluxeMapping will be set by R_LoadEntities()
	tr.worldDeluxeMapping = false;
	tr.worldHDR_RGBE = false;

	memset( &s_worldData, 0, sizeof( s_worldData ) );
	Q_strncpyz( s_worldData.name, name, sizeof( s_worldData.name ) );

	Q_strncpyz( s_worldData.baseName, COM_SkipPath( s_worldData.name ), sizeof( s_worldData.name ) );
	COM_StripExtension3( s_worldData.baseName, s_worldData.baseName, sizeof( s_worldData.baseName ) );

	startMarker = (byte*) ri.Hunk_Alloc( 0, ha_pref::h_low );

	header = ( dheader_t * ) buffer.data();
	fileBase = ( byte * ) header;

	i = LittleLong( header->version );

	if ( i != BSP_VERSION && i != BSP_VERSION_Q3 )
	{
		Sys::Drop( "RE_LoadWorldMap: %s has wrong version number (%i should be %i for ET or %i for Q3)",
		           name, i, BSP_VERSION, BSP_VERSION_Q3 );
	}

	// swap all the lumps
	for ( unsigned j = 0; j < sizeof( dheader_t ) / 4; j++ )
	{
		( ( int * ) header ) [ j ] = LittleLong( ( ( int * ) header ) [ j ] );
	}

	// load into heap
	R_LoadEntities( &header->lumps[ LUMP_ENTITIES ] );

	R_LoadShaders( &header->lumps[ LUMP_SHADERS ] );

	R_LoadLightmaps( &header->lumps[ LUMP_LIGHTMAPS ], name );

	R_LoadPlanes( &header->lumps[ LUMP_PLANES ] );

	R_LoadSurfaces( &header->lumps[ LUMP_SURFACES ], &header->lumps[ LUMP_DRAWVERTS ], &header->lumps[ LUMP_DRAWINDEXES ] );

	R_LoadMarksurfaces( &header->lumps[ LUMP_LEAFSURFACES ] );

	R_LoadNodesAndLeafs( &header->lumps[ LUMP_NODES ], &header->lumps[ LUMP_LEAFS ] );

	R_LoadSubmodels( &header->lumps[ LUMP_MODELS ] );

	// moved fog lump loading here, so fogs can be tagged with a model num
	R_LoadFogs( &header->lumps[ LUMP_FOGS ], &header->lumps[ LUMP_BRUSHES ], &header->lumps[ LUMP_BRUSHSIDES ] );

	R_LoadVisibility( &header->lumps[ LUMP_VISIBILITY ] );

	R_LoadLightGrid( &header->lumps[ LUMP_LIGHTGRID ] );

	// create a static vbo for the world
	R_CreateWorldVBO();
	R_CreateClusters();

	// we precache interactions between lights and surfaces
	// to reduce the polygon count
	R_PrecacheInteractions();

	s_worldData.dataSize = ( byte * ) ri.Hunk_Alloc( 0, ha_pref::h_low ) - startMarker;

	// only set tr.world now that we know the entire level has loaded properly
	tr.world = &s_worldData;

	// build cubemaps after the necessary vbo stuff is done
	//R_BuildCubeMaps();

	tr.worldLight = tr.lightMode;
	tr.modelDeluxe = deluxeMode_t::NONE;

	if ( tr.worldLight == lightMode_t::FULLBRIGHT )
	{
		tr.modelLight = lightMode_t::FULLBRIGHT;
	}
	else
	{
		if ( tr.worldLight == lightMode_t::MAP )
		{
			// World surfaces use light mapping.

			if ( !tr.worldLightMapping )
			{
				/* Use vertex light as a fallback on world surfaces missing a light map,
				q3map2 has an option to produce less lightmap files by skipping them when
				they are very similar to the vertex color. The vertex color is expected
				to match the color of the nearby lightmaps. We better not want to use
				the grid light as a fallback as it would be close but not close enough. */

				tr.worldLight = lightMode_t::VERTEX;
			}
		}

		if ( tr.worldDeluxeMapping )
		{
			if ( tr.worldLight == lightMode_t::MAP )
			{
				tr.worldDeluxe = deluxeMode_t::MAP;
			}

			/* The combination of grid light and deluxe map is
			technically doable, but rendering the world with a
			light grid while a light map is available is not
			the experience we want to provide, so we don't
			allow this combination to not compile the related
			shaders. */
		}

		/* We can technically use emulated deluxe map from light direction dir
		on surfaces with light map but no deluxe map, but this is ugly.
		Also, enabling it would require to make some macro not conflicting and
		then would increase the amount of GLSL shader variants to be compiled,
		this to render legacy maps in a way legacy renderers never rendered them.
		It could still be cool as an optional feature, if we use a better
		algorithm for emulating the deluxe map from light direction grid.
		See https://github.com/DaemonEngine/Daemon/issues/32 */

		// Game model surfaces use grid lighting, they don't have vertex light colors.
		tr.modelLight = lightMode_t::GRID;

		if ( !tr.lightGrid1Image )
		{
			// Use fullbright light on game models if light color grid is missing.
			tr.modelLight = lightMode_t::FULLBRIGHT;

			if ( tr.worldLight == lightMode_t::GRID )
			{
				// Use vertex light on world surface if light color grid is missing.
				tr.worldLight = lightMode_t::VERTEX;
			}
		}

		if ( r_deluxeMapping->integer )
		{
			// Enable deluxe mapping emulation if light direction grid is there.
			if ( tr.lightGrid2Image )
			{
				// Game model surfaces use grid lighting, they don't have vertex light colors.
				tr.modelDeluxe = deluxeMode_t::GRID;

				// Only game models use emulated deluxe map from light direction grid.
			}
		}
	}
}
