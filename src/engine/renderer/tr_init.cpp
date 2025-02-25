/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.
Copyright (C) 2006-2011 Robert Beckebans <trebor_7@users.sourceforge.net>

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
// tr_init.c -- functions that are not called every frame
#include "tr_local.h"
#include "framework/CvarSystem.h"

	glconfig_t  glConfig;
	glconfig2_t glConfig2;

	glstate_t   glState;

	float       displayAspect = 0.0f;

	static void GfxInfo_f();

	cvar_t      *r_glMajorVersion;
	cvar_t      *r_glMinorVersion;
	cvar_t      *r_glProfile;
	cvar_t      *r_glDebugProfile;
	cvar_t      *r_glDebugMode;
	cvar_t      *r_glAllowSoftware;
	cvar_t      *r_glExtendedValidation;

	cvar_t      *r_verbose;
	cvar_t      *r_ignore;

	cvar_t      *r_znear;
	cvar_t      *r_zfar;

	cvar_t      *r_smp;
	cvar_t      *r_showSmp;
	cvar_t      *r_skipBackEnd;
	cvar_t      *r_skipLightBuffer;

	cvar_t      *r_measureOverdraw;

	cvar_t      *r_fastsky;

	cvar_t      *r_lodBias;
	cvar_t      *r_lodScale;
	cvar_t      *r_lodTest;

	cvar_t      *r_norefresh;
	cvar_t      *r_drawentities;
	cvar_t      *r_drawworld;
	cvar_t      *r_drawpolies;
	cvar_t      *r_speeds;
	cvar_t      *r_novis;
	cvar_t      *r_nocull;
	cvar_t      *r_facePlaneCull;
	cvar_t      *r_nocurves;
	cvar_t      *r_lightScissors;
	cvar_t      *r_noLightVisCull;
	cvar_t      *r_noInteractionSort;
	cvar_t      *r_dynamicLight;
	cvar_t      *r_staticLight;
	cvar_t      *r_dynamicLightCastShadows;
	cvar_t      *r_precomputedLighting;
	Cvar::Cvar<int> r_mapOverBrightBits("r_mapOverBrightBits", "default map light color shift", Cvar::NONE, 2);
	Cvar::Range<Cvar::Cvar<int>> r_lightMode("r_lightMode", "lighting mode: 0: fullbright (cheat), 1: vertex light, 2: grid light (cheat), 3: light map", Cvar::NONE, Util::ordinal(lightMode_t::MAP), Util::ordinal(lightMode_t::FULLBRIGHT), Util::ordinal(lightMode_t::MAP));
	cvar_t      *r_lightStyles;
	cvar_t      *r_exportTextures;
	cvar_t      *r_heatHaze;
	cvar_t      *r_noMarksOnTrisurfs;
	cvar_t      *r_lazyShaders;

	cvar_t      *r_ext_occlusion_query;
	cvar_t      *r_ext_draw_buffers;
	cvar_t      *r_ext_vertex_array_object;
	cvar_t      *r_ext_half_float_pixel;
	cvar_t      *r_ext_texture_float;
	cvar_t      *r_ext_texture_integer;
	cvar_t      *r_ext_texture_rg;
	cvar_t      *r_ext_texture_filter_anisotropic;
	cvar_t      *r_ext_gpu_shader4;
	cvar_t      *r_arb_buffer_storage;
	cvar_t      *r_arb_map_buffer_range;
	cvar_t      *r_arb_sync;
	cvar_t      *r_arb_uniform_buffer_object;
	cvar_t      *r_arb_texture_gather;
	cvar_t      *r_arb_gpu_shader5;

	cvar_t      *r_checkGLErrors;
	cvar_t      *r_logFile;

	cvar_t      *r_depthbits;
	cvar_t      *r_colorbits;

	cvar_t      *r_drawBuffer;
	cvar_t      *r_shadows;
	cvar_t      *r_softShadows;
	cvar_t      *r_softShadowsPP;
	cvar_t      *r_shadowBlur;

	cvar_t      *r_shadowMapQuality;
	cvar_t      *r_shadowMapSizeUltra;
	cvar_t      *r_shadowMapSizeVeryHigh;
	cvar_t      *r_shadowMapSizeHigh;
	cvar_t      *r_shadowMapSizeMedium;
	cvar_t      *r_shadowMapSizeLow;

	cvar_t      *r_shadowMapSizeSunUltra;
	cvar_t      *r_shadowMapSizeSunVeryHigh;
	cvar_t      *r_shadowMapSizeSunHigh;
	cvar_t      *r_shadowMapSizeSunMedium;
	cvar_t      *r_shadowMapSizeSunLow;

	cvar_t      *r_shadowOffsetFactor;
	cvar_t      *r_shadowOffsetUnits;
	cvar_t      *r_shadowLodBias;
	cvar_t      *r_shadowLodScale;
	cvar_t      *r_noShadowPyramids;
	cvar_t      *r_cullShadowPyramidFaces;
	cvar_t      *r_cullShadowPyramidCurves;
	cvar_t      *r_cullShadowPyramidTriangles;
	cvar_t      *r_debugShadowMaps;
	cvar_t      *r_noShadowFrustums;
	cvar_t      *r_noLightFrustums;
	cvar_t      *r_shadowMapLinearFilter;
	cvar_t      *r_lightBleedReduction;
	cvar_t      *r_overDarkeningFactor;
	cvar_t      *r_shadowMapDepthScale;
	cvar_t      *r_parallelShadowSplits;
	cvar_t      *r_parallelShadowSplitWeight;

	cvar_t      *r_mode;
	cvar_t      *r_nobind;
	cvar_t      *r_singleShader;
	cvar_t      *r_colorMipLevels;
	cvar_t      *r_picMip;
	cvar_t      *r_imageMaxDimension;
	cvar_t      *r_ignoreMaterialMinDimension;
	cvar_t      *r_ignoreMaterialMaxDimension;
	cvar_t      *r_replaceMaterialMinDimensionIfPresentWithMaxDimension;
	cvar_t      *r_finish;
	cvar_t      *r_clear;
	cvar_t      *r_textureMode;
	cvar_t      *r_offsetFactor;
	cvar_t      *r_offsetUnits;

	cvar_t      *r_physicalMapping;
	cvar_t      *r_specularExponentMin;
	cvar_t      *r_specularExponentMax;
	cvar_t      *r_specularScale;
	cvar_t      *r_specularMapping;
	cvar_t      *r_deluxeMapping;
	cvar_t      *r_normalScale;
	cvar_t      *r_normalMapping;
	cvar_t      *r_liquidMapping;
	cvar_t      *r_highQualityNormalMapping;
	cvar_t      *r_reliefDepthScale;
	cvar_t      *r_reliefMapping;
	cvar_t      *r_glowMapping;
	cvar_t      *r_reflectionMapping;

	cvar_t      *r_halfLambertLighting;
	cvar_t      *r_rimLighting;
	cvar_t      *r_rimExponent;
	cvar_t      *r_gamma;
	cvar_t      *r_lockpvs;
	cvar_t      *r_noportals;
	cvar_t      *r_portalOnly;

	cvar_t      *r_portalSky;
	cvar_t      *r_max_portal_levels;

	cvar_t      *r_subdivisions;
	cvar_t      *r_stitchCurves;

	Cvar::Modified<Cvar::Cvar<bool>> r_fullscreen(
		"r_fullscreen", "use full-screen window", CVAR_ARCHIVE, true );

	cvar_t      *r_customwidth;
	cvar_t      *r_customheight;

	cvar_t      *r_debugSurface;

	cvar_t      *r_showImages;

	cvar_t      *r_forceFog;
	cvar_t      *r_wolfFog;
	cvar_t      *r_noFog;

	cvar_t      *r_forceAmbient;
	cvar_t      *r_ambientScale;
	cvar_t      *r_lightScale;
	cvar_t      *r_debugSort;
	cvar_t      *r_printShaders;

	cvar_t      *r_maxPolys;
	cvar_t      *r_maxPolyVerts;

	cvar_t      *r_showTris;
	cvar_t      *r_showSky;
	cvar_t      *r_showShadowVolumes;
	cvar_t      *r_showShadowLod;
	cvar_t      *r_showShadowMaps;
	cvar_t      *r_showSkeleton;
	cvar_t      *r_showEntityTransforms;
	cvar_t      *r_showLightTransforms;
	cvar_t      *r_showLightInteractions;
	cvar_t      *r_showLightScissors;
	cvar_t      *r_showLightBatches;
	cvar_t      *r_showLightGrid;
	cvar_t      *r_showLightTiles;
	cvar_t      *r_showBatches;
	Cvar::Cvar<bool> r_showVertexColors("r_showVertexColors", "show vertex colors used for vertex lighting", Cvar::CHEAT, false);
	cvar_t      *r_showLightMaps;
	cvar_t      *r_showDeluxeMaps;
	cvar_t      *r_showNormalMaps;
	cvar_t      *r_showMaterialMaps;
	cvar_t      *r_showAreaPortals;
	cvar_t      *r_showCubeProbes;
	cvar_t      *r_showBspNodes;
	cvar_t      *r_showParallelShadowSplits;
	cvar_t      *r_showDecalProjectors;

	cvar_t      *r_vboFaces;
	cvar_t      *r_vboCurves;
	cvar_t      *r_vboTriangles;
	cvar_t      *r_vboShadows;
	cvar_t      *r_vboLighting;
	cvar_t      *r_vboModels;
	cvar_t      *r_vboVertexSkinning;
	cvar_t      *r_vboDeformVertexes;

	cvar_t      *r_mergeLeafSurfaces;

	cvar_t      *r_bloom;
	cvar_t      *r_bloomBlur;
	cvar_t      *r_bloomPasses;
	cvar_t      *r_FXAA;
	cvar_t      *r_ssao;

	cvar_t      *r_evsmPostProcess;

	void AssertCvarRange( cvar_t *cv, float minVal, float maxVal, bool shouldBeIntegral )
	{
		if ( shouldBeIntegral )
		{
			if ( cv->value != static_cast<float>(cv->integer) )
			{
				Log::Warn("cvar '%s' must be integral (%f)", cv->name, cv->value );
				Cvar_Set( cv->name, va( "%d", cv->integer ) );
			}
		}

		if ( cv->value < minVal )
		{
			Log::Warn("cvar '%s' out of range (%g < %g)", cv->name, cv->value, minVal );
			Cvar_Set( cv->name, va( "%f", minVal ) );
		}
		else if ( cv->value > maxVal )
		{
			Log::Warn("cvar '%s' out of range (%g > %g)", cv->name, cv->value, maxVal );
			Cvar_Set( cv->name, va( "%f", maxVal ) );
		}
	}

	/*
	** InitOpenGL
	**
	** This function is responsible for initializing a valid OpenGL subsystem.  This
	** is done by calling GLimp_Init (which gives us a working OGL subsystem) then
	** setting variables, checking GL constants, and reporting the gfx system config
	** to the user.
	*/
	static bool InitOpenGL()
	{
		char renderer_buffer[ 1024 ];

		//
		// initialize OS specific portions of the renderer
		//
		// GLimp_Init directly or indirectly references the following cvars:
		//      - r_fullscreen
		//      - r_mode
		//      - r_(color|depth|stencil)bits
		//

		if ( glConfig.vidWidth == 0 )
		{
			GLint temp;

			if ( !GLimp_Init() )
			{
				return false;
			}

			if( glConfig2.glCoreProfile ) {
				glGenVertexArrays( 1, &backEnd.currentVAO );
				glBindVertexArray( backEnd.currentVAO );
			}

			glState.tileStep[ 0 ] = TILE_SIZE * ( 1.0f / glConfig.vidWidth );
			glState.tileStep[ 1 ] = TILE_SIZE * ( 1.0f / glConfig.vidHeight );

			GL_CheckErrors();

			strcpy( renderer_buffer, glConfig.renderer_string );
			Q_strlwr( renderer_buffer );

			// OpenGL driver constants
			glGetIntegerv( GL_MAX_TEXTURE_SIZE, &temp );
			glConfig.maxTextureSize = temp;

			// stubbed or broken drivers may have reported 0...
			if ( glConfig.maxTextureSize <= 0 )
			{
				glConfig.maxTextureSize = 0;
			}

			// handle any OpenGL/GLSL brokenness here...
			// nothing at present

			GLSL_InitGPUShaders();
			glConfig.smpActive = false;

			if ( r_smp->integer )
			{
				Log::Notice("Trying SMP acceleration..." );

				if ( GLimp_SpawnRenderThread( RB_RenderThread ) )
				{
					Log::Notice("...succeeded." );
					glConfig.smpActive = true;
				}
				else
				{
					Log::Notice("...failed." );
				}
			}
		}
		else
		{
			GLSL_ShutdownGPUShaders();
			GLSL_InitGPUShaders();
		}

		GL_CheckErrors();

		// set default state
		GL_SetDefaultState();
		GL_CheckErrors();

		return true;
	}

	/*
	==================
	GL_CheckErrors

	Must not be called while the backend rendering thread is running
	==================
	*/
	void GL_CheckErrors_( const char *fileName, int line )
	{
		int  err;
		char s[ 128 ];

		if ( !checkGLErrors() )
		{
			return;
		}

		while ( ( err = glGetError() ) != GL_NO_ERROR )
		{
			switch ( err )
			{
				case GL_INVALID_ENUM:
					strcpy( s, "GL_INVALID_ENUM" );
					break;

				case GL_INVALID_VALUE:
					strcpy( s, "GL_INVALID_VALUE" );
					break;

				case GL_INVALID_OPERATION:
					strcpy( s, "GL_INVALID_OPERATION" );
					break;

				case GL_STACK_OVERFLOW:
					strcpy( s, "GL_STACK_OVERFLOW" );
					break;

				case GL_STACK_UNDERFLOW:
					strcpy( s, "GL_STACK_UNDERFLOW" );
					break;

				case GL_OUT_OF_MEMORY:
					strcpy( s, "GL_OUT_OF_MEMORY" );
					break;

				case GL_TABLE_TOO_LARGE:
					strcpy( s, "GL_TABLE_TOO_LARGE" );
					break;

				case GL_INVALID_FRAMEBUFFER_OPERATION:
					strcpy( s, "GL_INVALID_FRAMEBUFFER_OPERATION" );
					break;

				default:
					Com_sprintf( s, sizeof( s ), "0x%X", err );
					break;
			}
			// Pre-format the string so that each callsite counts separately for log suppression
			std::string error = Str::Format("OpenGL error %s detected at %s:%d", s, fileName, line);
			Log::Warn(error);
		}
	}

	/*
	** R_GetModeInfo
	*/
	struct vidmode_t
	{
		const char *description;
		int        width, height;
		float      pixelAspect; // pixel width / height
	};

	static const vidmode_t r_vidModes[] =
	{
		{ " 320x240",           320,  240, 1 },
		{ " 400x300",           400,  300, 1 },
		{ " 512x384",           512,  384, 1 },
		{ " 640x480",           640,  480, 1 },
		{ " 800x600",           800,  600, 1 },
		{ " 960x720",           960,  720, 1 },
		{ "1024x768",          1024,  768, 1 },
		{ "1152x864",          1152,  864, 1 },
		{ "1280x720  (16:9)",  1280,  720, 1 },
		{ "1280x768  (16:10)", 1280,  768, 1 },
		{ "1280x800  (16:10)", 1280,  800, 1 },
		{ "1280x1024",         1280, 1024, 1 },
		{ "1360x768  (16:9)",  1360,  768,  1 },
		{ "1440x900  (16:10)", 1440,  900, 1 },
		{ "1680x1050 (16:10)", 1680, 1050, 1 },
		{ "1600x1200",         1600, 1200, 1 },
		{ "1920x1080 (16:9)",  1920, 1080, 1 },
		{ "1920x1200 (16:10)", 1920, 1200, 1 },
		{ "2048x1536",         2048, 1536, 1 },
		{ "2560x1600 (16:10)", 2560, 1600, 1 },
	};
	static const int s_numVidModes = ARRAY_LEN( r_vidModes );

	bool R_GetModeInfo( int *width, int *height, int mode )
	{
		const vidmode_t *vm;

		if ( mode < -2 )
		{
			return false;
		}

		if ( mode >= s_numVidModes )
		{
			return false;
		}

		if( mode == -2)
		{
			// Must set width and height to display size before calling this function!
		}
		else if ( mode == -1 )
		{
			*width = r_customwidth->integer;
			*height = r_customheight->integer;
		}
		else
		{
			vm = &r_vidModes[ mode ];

			*width = vm->width;
			*height = vm->height;
		}

		return true;
	}

	/*
	** R_ModeList_f
	*/
	static void R_ModeList_f()
	{
		int i;

		for ( i = 0; i < s_numVidModes; i++ )
		{
			Log::Notice("Mode %-2d: %s", i, r_vidModes[ i ].description );
		}
	}

	/*
	==================
	RB_ReadPixels

	Reads an image but takes care of alignment issues for reading RGB images.
	Prepends the specified number of (uninitialized) bytes to the buffer.

	The returned buffer must be freed with ri.Hunk_FreeTempMemory().
	==================
	*/
	static byte *RB_ReadPixels( int x, int y, int width, int height, size_t offset )
	{
		GLint packAlign;
		int   lineLen, paddedLineLen;
		byte  *buffer, *pixels;
		int   i;

		glGetIntegerv( GL_PACK_ALIGNMENT, &packAlign );

		lineLen = width * 3;
		paddedLineLen = PAD( lineLen, packAlign );

		// Allocate a few more bytes so that we can choose an alignment we like
		buffer = ( byte * ) ri.Hunk_AllocateTempMemory( offset + paddedLineLen * height + packAlign - 1 );

		pixels = ( byte * ) PADP( buffer + offset, packAlign );
		glReadPixels( x, y, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels );

		// Drop alignment and line padding bytes
		for ( i = 0; i < height; ++i )
		{
			memmove( buffer + offset + i * lineLen, pixels + i * paddedLineLen, lineLen );
		}

		return buffer;
	}

	/*
	==================
	RB_TakeScreenshot
	==================
	*/
	static void RB_TakeScreenshot( int x, int y, int width, int height, const char *fileName )
	{
		byte *buffer;
		int  dataSize;
		byte *end, *p;

		// with 18 bytes for the TGA file header
		buffer = RB_ReadPixels( x, y, width, height, 18 );
		memset( buffer, 0, 18 );

		buffer[ 2 ] = 2; // uncompressed type
		buffer[ 12 ] = width & 255;
		buffer[ 13 ] = width >> 8;
		buffer[ 14 ] = height & 255;
		buffer[ 15 ] = height >> 8;
		buffer[ 16 ] = 24; // pixel size

		dataSize = 3 * width * height;

		// swap RGB to BGR
		end = buffer + 18 + dataSize;

		for ( p = buffer + 18; p < end; p += 3 )
		{
			byte temp = p[ 0 ];
			p[ 0 ] = p[ 2 ];
			p[ 2 ] = temp;
		}

		ri.FS_WriteFile( fileName, buffer, 18 + dataSize );

		ri.Hunk_FreeTempMemory( buffer );
	}

	/*
	==================
	RB_TakeScreenshotJPEG
	==================
	*/
	static void RB_TakeScreenshotJPEG( int x, int y, int width, int height, const char *fileName )
	{
		byte *buffer = RB_ReadPixels( x, y, width, height, 0 );

		SaveJPG( fileName, 90, width, height, buffer );
		ri.Hunk_FreeTempMemory( buffer );
	}

	/*
	==================
	RB_TakeScreenshotPNG
	==================
	*/
	static void RB_TakeScreenshotPNG( int x, int y, int width, int height, const char *fileName )
	{
		byte *buffer = RB_ReadPixels( x, y, width, height, 0 );

		SavePNG( fileName, buffer, width, height, 3, false );
		ri.Hunk_FreeTempMemory( buffer );
	}

	/*
	==================
	RB_TakeScreenshotCmd
	==================
	*/
	const RenderCommand *ScreenshotCommand::ExecuteSelf( ) const
	{
		switch ( format )
		{
			case ssFormat_t::SSF_TGA:
				RB_TakeScreenshot( x, y, width, height, fileName );
				break;

			case ssFormat_t::SSF_JPEG:
				RB_TakeScreenshotJPEG( x, y, width, height, fileName );
				break;

			case ssFormat_t::SSF_PNG:
				RB_TakeScreenshotPNG( x, y, width, height, fileName );
				break;
		}

		return this + 1;
	}

	/*
	==================
	R_TakeScreenshot
	==================
	*/
	static bool R_TakeScreenshot( Str::StringRef path, ssFormat_t format )
	{
		ScreenshotCommand *cmd = R_GetRenderCommand<ScreenshotCommand>();

		if ( !cmd )
		{
			return false;
		}

		cmd->x = 0;
		cmd->y = 0;
		cmd->width = glConfig.vidWidth;
		cmd->height = glConfig.vidHeight;
		Q_strncpyz(cmd->fileName, path.c_str(), sizeof(cmd->fileName));
		cmd->format = format;

		return true;
	}

namespace {
class ScreenshotCmd : public Cmd::StaticCmd {
	const ssFormat_t format;
	const std::string fileExtension;
public:
	ScreenshotCmd(std::string cmdName, ssFormat_t format, std::string ext)
		: StaticCmd(cmdName, Cmd::RENDERER, Str::Format("take a screenshot in %s format", ext)),
		format(format), fileExtension(ext) {}

	void Run(const Cmd::Args& args) const override {
		if (!tr.registered) {
			Print("ScreenshotCmd: renderer not initialized");
			return;
		}
		if (args.Argc() > 2) {
			PrintUsage(args, "[name]");
			return;
		}

		std::string fileName;
		if ( args.Argc() == 2 )
		{
			fileName = Str::Format( "screenshots/" PRODUCT_NAME_LOWER "-%s.%s", args.Argv(1), fileExtension );
		}
		else
		{
			qtime_t t;
			ri.RealTime( &t );

			// scan for a free filename
			int lastNumber;
			for ( lastNumber = 0; lastNumber <= 999; lastNumber++ )
			{
				fileName = Str::Format( "screenshots/" PRODUCT_NAME_LOWER "_%04d-%02d-%02d_%02d%02d%02d_%03d.%s",
					                    1900 + t.tm_year, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, lastNumber, fileExtension );

				if ( !FS::HomePath::FileExists( fileName.c_str() ) )
				{
					break; // file doesn't exist
				}
			}

			if ( lastNumber == 1000 )
			{
				Print("ScreenshotCmd: Couldn't create a file" );
				return;
			}
		}

		if (R_TakeScreenshot(fileName, format))
		{
			Print("Wrote %s", fileName);
		}
	}
};
ScreenshotCmd screenshotTGARegistration("screenshot", ssFormat_t::SSF_TGA, "tga");
ScreenshotCmd screenshotJPEGRegistration("screenshotJPEG", ssFormat_t::SSF_JPEG, "jpg");
ScreenshotCmd screenshotPNGRegistration("screenshotPNG", ssFormat_t::SSF_PNG, "png");
} // namespace

//============================================================================

	/*
	==================
	RB_TakeVideoFrameCmd
	==================
	*/
	const RenderCommand *VideoFrameCommand::ExecuteSelf( ) const
	{
		GLint                     packAlign;
		int                       lineLen, captureLineLen;
		byte                      *pixels;
		int                       i;
		int                       outputSize;
		int                       j;
		int                       aviLineLen;

		// RB: it is possible to we still have a videoFrameCommand_t but we already stopped
		// video recording
		if ( ri.CL_VideoRecording() )
		{
			// take care of alignment issues for reading RGB images..

			glGetIntegerv( GL_PACK_ALIGNMENT, &packAlign );

			lineLen = width * 3;
			captureLineLen = PAD( lineLen, packAlign );

			pixels = ( byte * ) PADP( captureBuffer, packAlign );
			glReadPixels( 0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels );

			if ( motionJpeg )
			{
				// Drop alignment and line padding bytes
				for ( i = 0; i < height; ++i )
				{
					memmove( captureBuffer + i * lineLen, pixels + i * captureLineLen, lineLen );
				}

				outputSize = SaveJPGToBuffer( encodeBuffer, 3 * width * height, 90, width, height, captureBuffer );
				ri.CL_WriteAVIVideoFrame( encodeBuffer, outputSize );
			}
			else
			{
				aviLineLen = PAD( lineLen, AVI_LINE_PADDING );

				for ( i = 0; i < height; ++i )
				{
					for ( j = 0; j < lineLen; j += 3 )
					{
						encodeBuffer[ i * aviLineLen + j + 0 ] = pixels[ i * captureLineLen + j + 2 ];
						encodeBuffer[ i * aviLineLen + j + 1 ] = pixels[ i * captureLineLen + j + 1 ];
						encodeBuffer[ i * aviLineLen + j + 2 ] = pixels[ i * captureLineLen + j + 0 ];
					}

					while ( j < aviLineLen )
					{
						encodeBuffer[ i * aviLineLen + j++ ] = 0;
					}
				}

				ri.CL_WriteAVIVideoFrame( encodeBuffer, aviLineLen * height );
			}
		}

		return this + 1;
	}

//============================================================================

	/*
	** GL_SetDefaultState
	*/
	void GL_SetDefaultState()
	{
		int i;

		GLimp_LogComment( "--- GL_SetDefaultState ---\n" );

		GL_ClearDepth( 1.0f );
		GL_ClearStencil( 0 );

		GL_FrontFace( GL_CCW );
		GL_CullFace( GL_FRONT );

		glState.faceCulling = CT_TWO_SIDED;
		glDisable( GL_CULL_FACE );

		GL_CheckErrors();

		glVertexAttrib4f( ATTR_INDEX_COLOR, 1, 1, 1, 1 );

		GL_CheckErrors();

		// initialize downstream texture units if we're running
		// in a multitexture environment
		if ( glConfig.driverType == glDriverType_t::GLDRV_OPENGL3 )
		{
			for ( i = 31; i >= 0; i-- )
			{
				GL_SelectTexture( i );
				GL_TextureMode( r_textureMode->string );
			}
		}

		GL_CheckErrors();

		GL_DepthFunc( GL_LEQUAL );

		// make sure our GL state vector is set correctly
		glState.glStateBits = GLS_DEPTHTEST_DISABLE | GLS_DEPTHMASK_TRUE;
		glState.vertexAttribsState = 0;
		glState.vertexAttribPointersSet = 0;

		GL_BindProgram( nullptr );

		glBindBuffer( GL_ARRAY_BUFFER, 0 );
		glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, 0 );
		glState.currentVBO = nullptr;
		glState.currentIBO = nullptr;

		GL_CheckErrors();

		// the vertex array is always enabled, but the color and texture
		// arrays are enabled and disabled around the compiled vertex array call
		glEnableVertexAttribArray( ATTR_INDEX_POSITION );

		/*
		   OpenGL 3.0 spec: E.1. PROFILES AND DEPRECATED FEATURES OF OPENGL 3.0 405
		   Calling VertexAttribPointer when no buffer object or no
		   vertex array object is bound will generate an INVALID OPERATION error,
		   as will calling any array drawing command when no vertex array object is
		   bound.
		 */

		GL_fboShim.glBindFramebuffer( GL_FRAMEBUFFER, 0 );
		GL_fboShim.glBindRenderbuffer( GL_RENDERBUFFER, 0 );
		glState.currentFBO = nullptr;

		GL_PolygonMode( GL_FRONT_AND_BACK, GL_FILL );
		GL_DepthMask( GL_TRUE );
		glDisable( GL_DEPTH_TEST );
		glEnable( GL_SCISSOR_TEST );
		glDisable( GL_BLEND );

		glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
		glClearColor( 0.0f, 0.0f, 0.0f, 1.0f );
		glClearDepth( 1.0 );

		glDrawBuffer( GL_BACK );
		glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );

		GL_CheckErrors();

		glState.stackIndex = 0;

		for ( i = 0; i < MAX_GLSTACK; i++ )
		{
			MatrixIdentity( glState.modelViewMatrix[ i ] );
			MatrixIdentity( glState.projectionMatrix[ i ] );
			MatrixIdentity( glState.modelViewProjectionMatrix[ i ] );
		}
	}

	/*
	================
	GfxInfo_f
	================
	*/
	void GfxInfo_f()
	{
		static const char fsstrings[][16] =
		{
			"windowed",
			"fullscreen"
		};

		Log::Notice("GL_VENDOR: %s", glConfig.vendor_string );
		Log::Notice("GL_RENDERER: %s", glConfig.renderer_string );
		Log::Notice("GL_VERSION: %s", glConfig.version_string );
		Log::Debug("GL_EXTENSIONS: %s", glConfig2.glExtensionsString );
		Log::Notice("GL_MAX_TEXTURE_SIZE: %d", glConfig.maxTextureSize );

		Log::Notice("GL_SHADING_LANGUAGE_VERSION: %s", glConfig2.shadingLanguageVersionString );

		Log::Notice("GL_MAX_VERTEX_UNIFORM_COMPONENTS %d", glConfig2.maxVertexUniforms );
		Log::Notice("GL_MAX_VERTEX_ATTRIBS %d", glConfig2.maxVertexAttribs );

		if ( glConfig2.occlusionQueryAvailable )
		{
			Log::Notice("Occlusion query bits: %d", glConfig2.occlusionQueryBits );
		}

		if ( glConfig2.drawBuffersAvailable )
		{
			Log::Notice("GL_MAX_DRAW_BUFFERS: %d", glConfig2.maxDrawBuffers );
		}

		if ( glConfig2.textureAnisotropyAvailable )
		{
			Log::Notice("GL_TEXTURE_MAX_ANISOTROPY_EXT: %f", glConfig2.maxTextureAnisotropy );
		}

		Log::Notice("GL_MAX_RENDERBUFFER_SIZE: %d", glConfig2.maxRenderbufferSize );
		Log::Notice("GL_MAX_COLOR_ATTACHMENTS: %d", glConfig2.maxColorAttachments );

		Log::Notice("PIXELFORMAT: color(%d-bits)", glConfig.colorBits );

		{
			std::string out;
			if ( glConfig.displayFrequency )
			{
				out = Str::Format("%d", glConfig.displayFrequency );
			}
			else
			{
				out = "N/A";
			}

			Log::Notice("MODE: %d, %d x %d %s hz: %s",
				r_mode->integer,
				glConfig.vidWidth, glConfig.vidHeight,
				fsstrings[ +r_fullscreen.Get() ],
				out
				);
		}

		if ( !!r_glExtendedValidation->integer )
		{
			Log::Notice("Using OpenGL version %d.%d, requested: %d.%d, highest: %d.%d",
				glConfig2.glMajor, glConfig2.glMinor, glConfig2.glRequestedMajor, glConfig2.glRequestedMinor,
				glConfig2.glHighestMajor, glConfig2.glHighestMinor );
		}
		else
		{
			Log::Notice("Using OpenGL version %d.%d, requested: %d.%d", glConfig2.glMajor, glConfig2.glMinor, glConfig2.glRequestedMajor, glConfig2.glRequestedMinor );
		}

		if ( glConfig.driverType == glDriverType_t::GLDRV_OPENGL3 )
		{
			Log::Notice("%sUsing OpenGL 3.x context.", Color::ToString( Color::Green ) );

			/* See https://www.khronos.org/opengl/wiki/OpenGL_Context
			for information about core, compatibility and forward context. */

			if ( glConfig2.glCoreProfile )
			{
				Log::Notice("%sUsing an OpenGL core profile.", Color::ToString( Color::Green ) );
			}
			else
			{
				Log::Notice("%sUsing an OpenGL compatibility profile.", Color::ToString( Color::Red ) );
			}

			if ( glConfig2.glForwardCompatibleContext )
			{
				Log::Notice("OpenGL 3.x context is forward compatible.");
			}
			else
			{
				Log::Notice("OpenGL 3.x context is not forward compatible.");
			}
		}
		else
		{
			Log::Notice("%sUsing OpenGL 2.x context.", Color::ToString( Color::Red ) );
		}

		if ( glConfig2.glEnabledExtensionsString.length() != 0 )
		{
			Log::Notice("%sUsing OpenGL extensions: %s", Color::ToString( Color::Green ), glConfig2.glEnabledExtensionsString );
		}

		if ( glConfig2.glMissingExtensionsString.length() != 0 )
		{
			Log::Notice("%sMissing OpenGL extensions: %s", Color::ToString( Color::Red ), glConfig2.glMissingExtensionsString );
		}

		if ( glConfig.hardwareType == glHardwareType_t::GLHW_R300 )
		{
			Log::Notice("%sUsing ATI R300 approximations.", Color::ToString( Color::Red ));
		}

		if ( glConfig.textureCompression != textureCompression_t::TC_NONE )
		{
			Log::Notice("%sUsing S3TC (DXTC) texture compression.", Color::ToString( Color::Green ) );
		}

		if ( glConfig2.vboVertexSkinningAvailable )
		{
			/* Mesa drivers usually support 256 bones, Nvidia proprietary drivers
			usually support 233 bones, OpenGL 2.1 hardware usually supports no more
			than 41 bones which may not be enough to use hardware acceleration on
			models from games like Unvanquished. */
			if ( glConfig2.maxVertexSkinningBones < 233 )
			{
				Log::Notice("%sUsing GPU vertex skinning with max %i bones in a single pass, some models may not be hardware accelerated.", Color::ToString( Color::Red ), glConfig2.maxVertexSkinningBones );
			}
			else
			{
				Log::Notice("%sUsing GPU vertex skinning with max %i bones in a single pass, models are hardware accelerated.", Color::ToString( Color::Green ), glConfig2.maxVertexSkinningBones );
			}
		}
		else
		{
			Log::Notice("%sMissing GPU vertex skinning, models are not hardware-accelerated.", Color::ToString( Color::Red ) );
		}

		if ( glConfig.smpActive )
		{
			Log::Notice("Using dual processor acceleration." );
		}

		if ( r_finish->integer )
		{
			Log::Notice("Forcing glFinish." );
		}

		Log::Debug("texturemode: %s", r_textureMode->string );
		Log::Debug("picmip: %d", r_picMip->integer );
		Log::Debug("imageMaxDimension: %d", r_imageMaxDimension->integer );
		Log::Debug("ignoreMaterialMinDimension: %d", r_ignoreMaterialMinDimension->integer );
		Log::Debug("ignoreMaterialMaxDimension: %d", r_ignoreMaterialMaxDimension->integer );
		Log::Debug("replaceMaterialMinDimensionIfPresentWithMaxDimension: %d", r_replaceMaterialMinDimensionIfPresentWithMaxDimension->integer );
	}

	static void GLSL_restart_f()
	{
		// make sure the render thread is stopped
		R_SyncRenderThread();

		GLSL_ShutdownGPUShaders();
		GLSL_InitGPUShaders();
	}

	/*
	===============
	R_Register
	===============
	*/
	void R_Register()
	{
		// OpenGL context selection
		r_glMajorVersion = Cvar_Get( "r_glMajorVersion", "", CVAR_LATCH );
		r_glMinorVersion = Cvar_Get( "r_glMinorVersion", "", CVAR_LATCH );
		r_glProfile = Cvar_Get( "r_glProfile", "", CVAR_LATCH );
		r_glDebugProfile = Cvar_Get( "r_glDebugProfile", "", CVAR_LATCH );
		r_glDebugMode = Cvar_Get( "r_glDebugMode", "0", CVAR_CHEAT );
		r_glAllowSoftware = Cvar_Get( "r_glAllowSoftware", "0", CVAR_LATCH );
		r_glExtendedValidation = Cvar_Get( "r_glExtendedValidation", "0", CVAR_LATCH );

		// latched and archived variables
		r_ext_occlusion_query = Cvar_Get( "r_ext_occlusion_query", "1", CVAR_CHEAT | CVAR_LATCH );
		r_ext_draw_buffers = Cvar_Get( "r_ext_draw_buffers", "1", CVAR_CHEAT | CVAR_LATCH );
		r_ext_vertex_array_object = Cvar_Get( "r_ext_vertex_array_object", "1", CVAR_CHEAT | CVAR_LATCH );
		r_ext_half_float_pixel = Cvar_Get( "r_ext_half_float_pixel", "1", CVAR_CHEAT | CVAR_LATCH );
		r_ext_texture_float = Cvar_Get( "r_ext_texture_float", "1", CVAR_CHEAT | CVAR_LATCH );
		r_ext_texture_integer = Cvar_Get( "r_ext_texture_integer", "1", CVAR_CHEAT | CVAR_LATCH );
		r_ext_texture_rg = Cvar_Get( "r_ext_texture_rg", "1", CVAR_CHEAT | CVAR_LATCH );
		r_ext_texture_filter_anisotropic = Cvar_Get( "r_ext_texture_filter_anisotropic", "4",  CVAR_LATCH | CVAR_ARCHIVE );
		r_ext_gpu_shader4 = Cvar_Get( "r_ext_gpu_shader4", "1", CVAR_CHEAT | CVAR_LATCH );
		r_arb_buffer_storage = Cvar_Get( "r_arb_buffer_storage", "1", CVAR_CHEAT | CVAR_LATCH );
		r_arb_map_buffer_range = Cvar_Get( "r_arb_map_buffer_range", "1", CVAR_CHEAT | CVAR_LATCH );
		r_arb_sync = Cvar_Get( "r_arb_sync", "1", CVAR_CHEAT | CVAR_LATCH );
		r_arb_uniform_buffer_object = Cvar_Get( "r_arb_uniform_buffer_object", "1", CVAR_CHEAT | CVAR_LATCH );
		r_arb_texture_gather = Cvar_Get( "r_arb_texture_gather", "1", CVAR_CHEAT | CVAR_LATCH );
		r_arb_gpu_shader5 = Cvar_Get( "r_arb_gpu_shader5", "1", CVAR_CHEAT | CVAR_LATCH );

		r_picMip = Cvar_Get( "r_picMip", "0",  CVAR_LATCH | CVAR_ARCHIVE );
		r_imageMaxDimension = Cvar_Get( "r_imageMaxDimension", "0",  CVAR_LATCH | CVAR_ARCHIVE );
		r_ignoreMaterialMinDimension = Cvar_Get( "r_ignoreMaterialMinDimension", "0",  CVAR_LATCH | CVAR_ARCHIVE );
		r_ignoreMaterialMaxDimension = Cvar_Get( "r_ignoreMaterialMaxDimension", "0",  CVAR_LATCH | CVAR_ARCHIVE );
		r_replaceMaterialMinDimensionIfPresentWithMaxDimension
			= Cvar_Get( "r_replaceMaterialMinDimensionIfPresentWithMaxDimension", "0",  CVAR_LATCH | CVAR_ARCHIVE );
		r_colorMipLevels = Cvar_Get( "r_colorMipLevels", "0", CVAR_LATCH );
		r_colorbits = Cvar_Get( "r_colorbits", "0",  CVAR_LATCH );
		r_depthbits = Cvar_Get( "r_depthbits", "0",  CVAR_LATCH );
		r_mode = Cvar_Get( "r_mode", "-2", CVAR_LATCH | CVAR_ARCHIVE );
		r_customwidth = Cvar_Get( "r_customwidth", "1600", CVAR_LATCH | CVAR_ARCHIVE );
		r_customheight = Cvar_Get( "r_customheight", "1024", CVAR_LATCH | CVAR_ARCHIVE );
		r_subdivisions = Cvar_Get( "r_subdivisions", "4", CVAR_LATCH );
		r_dynamicLightCastShadows = Cvar_Get( "r_dynamicLightCastShadows", "1", 0 );
		r_precomputedLighting = Cvar_Get( "r_precomputedLighting", "1", CVAR_CHEAT | CVAR_LATCH );
		Cvar::Latch( r_mapOverBrightBits );
		Cvar::Latch( r_lightMode );
		r_lightStyles = Cvar_Get( "r_lightStyles", "1", CVAR_LATCH | CVAR_ARCHIVE );
		r_exportTextures = Cvar_Get( "r_exportTextures", "0", 0 );
		r_heatHaze = Cvar_Get( "r_heatHaze", "1", CVAR_LATCH | CVAR_ARCHIVE );
		r_noMarksOnTrisurfs = Cvar_Get( "r_noMarksOnTrisurfs", "1", CVAR_CHEAT );

		/* 1: Delay GLSL shader build until first map load.

		This makes possible for the user to quickly reach the game main menu
		without building all GLSL shaders and set a low graphics preset before
		joining a game and loading a map.

		Doing so prevents building unwanted or unsupported GLSL shaders on slow
		and/or old hardware and drastically reduce first startup time. */
		r_lazyShaders = Cvar_Get( "r_lazyShaders", "1", 0 );

		r_forceFog = Cvar_Get( "r_forceFog", "0", CVAR_CHEAT /* | CVAR_LATCH */ );
		AssertCvarRange( r_forceFog, 0.0f, 1.0f, false );
		r_wolfFog = Cvar_Get( "r_wolfFog", "1", CVAR_CHEAT );
		r_noFog = Cvar_Get( "r_noFog", "0", CVAR_CHEAT );

		r_forceAmbient = Cvar_Get( "r_forceAmbient", "0.125",  CVAR_LATCH );
		AssertCvarRange( r_forceAmbient, 0.0f, 0.3f, false );

		r_smp = Cvar_Get( "r_smp", "0",  CVAR_LATCH );

		// temporary latched variables that can only change over a restart
		r_singleShader = Cvar_Get( "r_singleShader", "0", CVAR_CHEAT | CVAR_LATCH );
		r_stitchCurves = Cvar_Get( "r_stitchCurves", "1", CVAR_CHEAT | CVAR_LATCH );
		r_debugShadowMaps = Cvar_Get( "r_debugShadowMaps", "0", CVAR_CHEAT | CVAR_LATCH );
		r_shadowMapLinearFilter = Cvar_Get( "r_shadowMapLinearFilter", "1", CVAR_CHEAT | CVAR_LATCH );
		r_lightBleedReduction = Cvar_Get( "r_lightBleedReduction", "0", CVAR_CHEAT | CVAR_LATCH );
		r_overDarkeningFactor = Cvar_Get( "r_overDarkeningFactor", "30.0", CVAR_CHEAT | CVAR_LATCH );
		r_shadowMapDepthScale = Cvar_Get( "r_shadowMapDepthScale", "1.41", CVAR_CHEAT | CVAR_LATCH );

		r_parallelShadowSplitWeight = Cvar_Get( "r_parallelShadowSplitWeight", "0.9", CVAR_CHEAT );
		r_parallelShadowSplits = Cvar_Get( "r_parallelShadowSplits", "2", CVAR_CHEAT | CVAR_LATCH );
		AssertCvarRange( r_parallelShadowSplits, 0, MAX_SHADOWMAPS - 1, true );

		// archived variables that can change at any time
		r_lodBias = Cvar_Get( "r_lodBias", "0", 0 );
		r_znear = Cvar_Get( "r_znear", "3", CVAR_CHEAT );
		r_zfar = Cvar_Get( "r_zfar", "0", CVAR_CHEAT );
		r_checkGLErrors = Cvar_Get( "r_checkGLErrors", "-1", 0 );
		r_fastsky = Cvar_Get( "r_fastsky", "0", CVAR_ARCHIVE );
		r_finish = Cvar_Get( "r_finish", "0", CVAR_CHEAT );
		r_textureMode = Cvar_Get( "r_textureMode", "GL_LINEAR_MIPMAP_LINEAR", CVAR_ARCHIVE );
		r_gamma = Cvar_Get( "r_gamma", "1.0", CVAR_ARCHIVE );
		r_facePlaneCull = Cvar_Get( "r_facePlaneCull", "1", 0 );

		r_ambientScale = Cvar_Get( "r_ambientScale", "1.0", CVAR_CHEAT | CVAR_LATCH );
		r_lightScale = Cvar_Get( "r_lightScale", "2", CVAR_CHEAT );

		r_vboFaces = Cvar_Get( "r_vboFaces", "1", CVAR_CHEAT );
		r_vboCurves = Cvar_Get( "r_vboCurves", "1", CVAR_CHEAT );
		r_vboTriangles = Cvar_Get( "r_vboTriangles", "1", CVAR_CHEAT );
		r_vboShadows = Cvar_Get( "r_vboShadows", "1", CVAR_CHEAT );
		r_vboLighting = Cvar_Get( "r_vboLighting", "1", CVAR_CHEAT );
		r_vboModels = Cvar_Get( "r_vboModels", "1", CVAR_LATCH );
		r_vboVertexSkinning = Cvar_Get( "r_vboVertexSkinning", "1",  CVAR_LATCH );
		r_vboDeformVertexes = Cvar_Get( "r_vboDeformVertexes", "1",  CVAR_LATCH );

		r_mergeLeafSurfaces = Cvar_Get( "r_mergeLeafSurfaces", "1",  CVAR_LATCH );

		r_evsmPostProcess = Cvar_Get( "r_evsmPostProcess", "0",  CVAR_LATCH );

		r_printShaders = Cvar_Get( "r_printShaders", "0", 0 );

		r_bloom = Cvar_Get( "r_bloom", "0", CVAR_LATCH | CVAR_ARCHIVE );
		r_bloomBlur = Cvar_Get( "r_bloomBlur", "1.0", CVAR_CHEAT );
		r_bloomPasses = Cvar_Get( "r_bloomPasses", "2", CVAR_CHEAT );
		r_FXAA = Cvar_Get( "r_FXAA", "0", CVAR_LATCH | CVAR_ARCHIVE );
		r_ssao = Cvar_Get( "r_ssao", "0", CVAR_LATCH | CVAR_ARCHIVE );

		// temporary variables that can change at any time
		r_showImages = Cvar_Get( "r_showImages", "0", CVAR_TEMP );

		r_debugSort = Cvar_Get( "r_debugSort", "0", CVAR_CHEAT );

		r_nocurves = Cvar_Get( "r_nocurves", "0", CVAR_CHEAT );
		r_lightScissors = Cvar_Get( "r_lightScissors", "1", CVAR_ARCHIVE );
		AssertCvarRange( r_lightScissors, 0, 2, true );

		r_noLightVisCull = Cvar_Get( "r_noLightVisCull", "0", CVAR_CHEAT );
		r_noInteractionSort = Cvar_Get( "r_noInteractionSort", "0", CVAR_CHEAT );
		r_dynamicLight = Cvar_Get( "r_dynamicLight", "2", CVAR_LATCH | CVAR_ARCHIVE );

		r_staticLight = Cvar_Get( "r_staticLight", "2", CVAR_ARCHIVE );
		r_drawworld = Cvar_Get( "r_drawworld", "1", CVAR_CHEAT );
		r_portalOnly = Cvar_Get( "r_portalOnly", "0", CVAR_CHEAT );
		r_portalSky = Cvar_Get( "cg_skybox", "1", 0 );
		r_max_portal_levels = Cvar_Get( "r_max_portal_levels", "5", 0 );

		r_showSmp = Cvar_Get( "r_showSmp", "0", CVAR_CHEAT );
		r_skipBackEnd = Cvar_Get( "r_skipBackEnd", "0", CVAR_CHEAT );
		r_skipLightBuffer = Cvar_Get( "r_skipLightBuffer", "0", CVAR_CHEAT );

		r_measureOverdraw = Cvar_Get( "r_measureOverdraw", "0", CVAR_CHEAT );
		r_lodScale = Cvar_Get( "r_lodScale", "5", CVAR_CHEAT );
		r_lodTest = Cvar_Get( "r_lodTest", "0.5", CVAR_CHEAT );
		r_norefresh = Cvar_Get( "r_norefresh", "0", CVAR_CHEAT );
		r_drawentities = Cvar_Get( "r_drawentities", "1", CVAR_CHEAT );
		r_drawpolies = Cvar_Get( "r_drawpolies", "1", CVAR_CHEAT );
		r_ignore = Cvar_Get( "r_ignore", "1", CVAR_CHEAT );
		r_nocull = Cvar_Get( "r_nocull", "0", CVAR_CHEAT );
		r_novis = Cvar_Get( "r_novis", "0", CVAR_CHEAT );
		r_speeds = Cvar_Get( "r_speeds", "0", 0 );
		r_verbose = Cvar_Get( "r_verbose", "0", CVAR_CHEAT );
		r_logFile = Cvar_Get( "r_logFile", "0", CVAR_CHEAT );
		r_debugSurface = Cvar_Get( "r_debugSurface", "0", CVAR_CHEAT );
		r_nobind = Cvar_Get( "r_nobind", "0", CVAR_CHEAT );
		r_clear = Cvar_Get( "r_clear", "1", 0 );
		r_offsetFactor = Cvar_Get( "r_offsetFactor", "-1", CVAR_CHEAT );
		r_offsetUnits = Cvar_Get( "r_offsetUnits", "-2", CVAR_CHEAT );

		r_physicalMapping = Cvar_Get( "r_physicalMapping", "1", CVAR_LATCH | CVAR_ARCHIVE );
		r_specularExponentMin = Cvar_Get( "r_specularExponentMin", "0.001", CVAR_CHEAT );
		r_specularExponentMax = Cvar_Get( "r_specularExponentMax", "16", CVAR_CHEAT );
		r_specularScale = Cvar_Get( "r_specularScale", "1.0", CVAR_CHEAT | CVAR_LATCH );
		r_specularMapping = Cvar_Get( "r_specularMapping", "1", CVAR_LATCH | CVAR_ARCHIVE );
		r_deluxeMapping = Cvar_Get( "r_deluxeMapping", "1", CVAR_LATCH | CVAR_ARCHIVE );
		r_normalScale = Cvar_Get( "r_normalScale", "1.0", CVAR_ARCHIVE );
		r_normalMapping = Cvar_Get( "r_normalMapping", "1", CVAR_LATCH | CVAR_ARCHIVE );
		r_highQualityNormalMapping = Cvar_Get( "r_highQualityNormalMapping", "0",  CVAR_LATCH );
		r_liquidMapping = Cvar_Get( "r_liquidMapping", "0", CVAR_LATCH | CVAR_ARCHIVE );
		r_reliefDepthScale = Cvar_Get( "r_reliefDepthScale", "0.03", CVAR_CHEAT );
		r_reliefMapping = Cvar_Get( "r_reliefMapping", "0", CVAR_LATCH | CVAR_ARCHIVE );
		r_glowMapping = Cvar_Get( "r_glowMapping", "1", CVAR_LATCH );
		r_reflectionMapping = Cvar_Get( "r_reflectionMapping", "0", CVAR_LATCH | CVAR_ARCHIVE );

		r_halfLambertLighting = Cvar_Get( "r_halfLambertLighting", "1", CVAR_LATCH | CVAR_ARCHIVE );
		r_rimLighting = Cvar_Get( "r_rimLighting", "0",  CVAR_LATCH | CVAR_ARCHIVE );
		r_rimExponent = Cvar_Get( "r_rimExponent", "3", CVAR_CHEAT | CVAR_LATCH );
		AssertCvarRange( r_rimExponent, 0.5, 8.0, false );

		r_drawBuffer = Cvar_Get( "r_drawBuffer", "GL_BACK", CVAR_CHEAT );
		r_lockpvs = Cvar_Get( "r_lockpvs", "0", CVAR_CHEAT );
		r_noportals = Cvar_Get( "r_noportals", "0", CVAR_CHEAT );

		r_shadows = Cvar_Get( "cg_shadows", "1",  CVAR_LATCH );
		AssertCvarRange( r_shadows, 0, Util::ordinal(shadowingMode_t::SHADOWING_EVSM32), true );

		r_softShadows = Cvar_Get( "r_softShadows", "0",  CVAR_LATCH );
		AssertCvarRange( r_softShadows, 0, 6, true );

		r_softShadowsPP = Cvar_Get( "r_softShadowsPP", "0",  CVAR_LATCH );

		r_shadowBlur = Cvar_Get( "r_shadowBlur", "2",  CVAR_LATCH );

		r_shadowMapQuality = Cvar_Get( "r_shadowMapQuality", "3",  CVAR_LATCH );
		AssertCvarRange( r_shadowMapQuality, 0, 4, true );

		r_shadowMapSizeUltra = Cvar_Get( "r_shadowMapSizeUltra", "1024",  CVAR_LATCH );
		AssertCvarRange( r_shadowMapSizeUltra, 32, 2048, true );

		r_shadowMapSizeVeryHigh = Cvar_Get( "r_shadowMapSizeVeryHigh", "512",  CVAR_LATCH );
		AssertCvarRange( r_shadowMapSizeVeryHigh, 32, 2048, true );

		r_shadowMapSizeHigh = Cvar_Get( "r_shadowMapSizeHigh", "256",  CVAR_LATCH );
		AssertCvarRange( r_shadowMapSizeHigh, 32, 2048, true );

		r_shadowMapSizeMedium = Cvar_Get( "r_shadowMapSizeMedium", "128",  CVAR_LATCH );
		AssertCvarRange( r_shadowMapSizeMedium, 32, 2048, true );

		r_shadowMapSizeLow = Cvar_Get( "r_shadowMapSizeLow", "64",  CVAR_LATCH );
		AssertCvarRange( r_shadowMapSizeLow, 32, 2048, true );

		shadowMapResolutions[ 0 ] = r_shadowMapSizeUltra->integer;
		shadowMapResolutions[ 1 ] = r_shadowMapSizeVeryHigh->integer;
		shadowMapResolutions[ 2 ] = r_shadowMapSizeHigh->integer;
		shadowMapResolutions[ 3 ] = r_shadowMapSizeMedium->integer;
		shadowMapResolutions[ 4 ] = r_shadowMapSizeLow->integer;

		r_shadowMapSizeSunUltra = Cvar_Get( "r_shadowMapSizeSunUltra", "1024",  CVAR_LATCH );
		AssertCvarRange( r_shadowMapSizeSunUltra, 32, 2048, true );

		r_shadowMapSizeSunVeryHigh = Cvar_Get( "r_shadowMapSizeSunVeryHigh", "1024",  CVAR_LATCH );
		AssertCvarRange( r_shadowMapSizeSunVeryHigh, 512, 2048, true );

		r_shadowMapSizeSunHigh = Cvar_Get( "r_shadowMapSizeSunHigh", "1024",  CVAR_LATCH );
		AssertCvarRange( r_shadowMapSizeSunHigh, 512, 2048, true );

		r_shadowMapSizeSunMedium = Cvar_Get( "r_shadowMapSizeSunMedium", "1024",  CVAR_LATCH );
		AssertCvarRange( r_shadowMapSizeSunMedium, 512, 2048, true );

		r_shadowMapSizeSunLow = Cvar_Get( "r_shadowMapSizeSunLow", "1024",  CVAR_LATCH );
		AssertCvarRange( r_shadowMapSizeSunLow, 512, 2048, true );

		sunShadowMapResolutions[ 0 ] = r_shadowMapSizeSunUltra->integer;
		sunShadowMapResolutions[ 1 ] = r_shadowMapSizeSunVeryHigh->integer;
		sunShadowMapResolutions[ 2 ] = r_shadowMapSizeSunHigh->integer;
		sunShadowMapResolutions[ 3 ] = r_shadowMapSizeSunMedium->integer;
		sunShadowMapResolutions[ 4 ] = r_shadowMapSizeSunLow->integer;

		r_shadowOffsetFactor = Cvar_Get( "r_shadowOffsetFactor", "0", CVAR_CHEAT );
		r_shadowOffsetUnits = Cvar_Get( "r_shadowOffsetUnits", "0", CVAR_CHEAT );
		r_shadowLodBias = Cvar_Get( "r_shadowLodBias", "0", CVAR_CHEAT );
		r_shadowLodScale = Cvar_Get( "r_shadowLodScale", "0.8", CVAR_CHEAT );
		r_noShadowPyramids = Cvar_Get( "r_noShadowPyramids", "0", CVAR_CHEAT );
		r_cullShadowPyramidFaces = Cvar_Get( "r_cullShadowPyramidFaces", "0", CVAR_CHEAT );
		r_cullShadowPyramidCurves = Cvar_Get( "r_cullShadowPyramidCurves", "1", CVAR_CHEAT );
		r_cullShadowPyramidTriangles = Cvar_Get( "r_cullShadowPyramidTriangles", "1", CVAR_CHEAT );
		r_noShadowFrustums = Cvar_Get( "r_noShadowFrustums", "0", CVAR_CHEAT );
		r_noLightFrustums = Cvar_Get( "r_noLightFrustums", "1", CVAR_CHEAT );

		r_maxPolys = Cvar_Get( "r_maxpolys", "10000", CVAR_LATCH );  // 600 in vanilla Q3A
		AssertCvarRange( r_maxPolys, 600, 30000, true );

		r_maxPolyVerts = Cvar_Get( "r_maxpolyverts", "100000", CVAR_LATCH );  // 3000 in vanilla Q3A
		AssertCvarRange( r_maxPolyVerts, 3000, 200000, true );

		r_showTris = Cvar_Get( "r_showTris", "0", CVAR_CHEAT );
		r_showSky = Cvar_Get( "r_showSky", "0", CVAR_CHEAT );
		r_showShadowVolumes = Cvar_Get( "r_showShadowVolumes", "0", CVAR_CHEAT );
		r_showShadowLod = Cvar_Get( "r_showShadowLod", "0", CVAR_CHEAT );
		r_showShadowMaps = Cvar_Get( "r_showShadowMaps", "0", CVAR_CHEAT );
		r_showSkeleton = Cvar_Get( "r_showSkeleton", "0", CVAR_CHEAT );
		r_showEntityTransforms = Cvar_Get( "r_showEntityTransforms", "0", CVAR_CHEAT );
		r_showLightTransforms = Cvar_Get( "r_showLightTransforms", "0", CVAR_CHEAT );
		r_showLightInteractions = Cvar_Get( "r_showLightInteractions", "0", CVAR_CHEAT );
		r_showLightScissors = Cvar_Get( "r_showLightScissors", "0", CVAR_CHEAT );
		r_showLightBatches = Cvar_Get( "r_showLightBatches", "0", CVAR_CHEAT );
		r_showLightGrid = Cvar_Get( "r_showLightGrid", "0", CVAR_CHEAT );
		r_showLightTiles = Cvar_Get("r_showLightTiles", "0", CVAR_CHEAT | CVAR_LATCH );
		r_showBatches = Cvar_Get( "r_showBatches", "0", CVAR_CHEAT );
		Cvar::Latch( r_showVertexColors );
		r_showLightMaps = Cvar_Get( "r_showLightMaps", "0", CVAR_CHEAT | CVAR_LATCH );
		r_showDeluxeMaps = Cvar_Get( "r_showDeluxeMaps", "0", CVAR_CHEAT | CVAR_LATCH );
		r_showNormalMaps = Cvar_Get( "r_showNormalMaps", "0", CVAR_CHEAT | CVAR_LATCH );
		r_showMaterialMaps = Cvar_Get( "r_showMaterialMaps", "0", CVAR_CHEAT | CVAR_LATCH );
		r_showAreaPortals = Cvar_Get( "r_showAreaPortals", "0", CVAR_CHEAT );
		r_showCubeProbes = Cvar_Get( "r_showCubeProbes", "0", CVAR_CHEAT );
		r_showBspNodes = Cvar_Get( "r_showBspNodes", "0", CVAR_CHEAT );
		r_showParallelShadowSplits = Cvar_Get( "r_showParallelShadowSplits", "0", CVAR_CHEAT | CVAR_LATCH );
		r_showDecalProjectors = Cvar_Get( "r_showDecalProjectors", "0", CVAR_CHEAT );

		// make sure all the commands added here are also removed in R_Shutdown
		ri.Cmd_AddCommand( "imagelist", R_ImageList_f );
		ri.Cmd_AddCommand( "shaderlist", R_ShaderList_f );
		ri.Cmd_AddCommand( "shaderexp", R_ShaderExp_f );
		ri.Cmd_AddCommand( "skinlist", R_SkinList_f );
		ri.Cmd_AddCommand( "modellist", R_Modellist_f );
		ri.Cmd_AddCommand( "modelist", R_ModeList_f );
		ri.Cmd_AddCommand( "animationlist", R_AnimationList_f );
		ri.Cmd_AddCommand( "fbolist", R_FBOList_f );
		ri.Cmd_AddCommand( "vbolist", R_VBOList_f );
		ri.Cmd_AddCommand( "gfxinfo", GfxInfo_f );
		ri.Cmd_AddCommand( "buildcubemaps", R_BuildCubeMaps );

		ri.Cmd_AddCommand( "glsl_restart", GLSL_restart_f );
	}

	/*
	===============
	R_Init
	===============
	*/
	bool R_Init()
	{
		int i;

		Log::Debug("----- R_Init -----" );

		// clear all our internal state
		ResetStruct( tr );
		ResetStruct( backEnd );
		ResetStruct( tess );

		if ( ( intptr_t ) tess.verts & 15 )
		{
			Log::Warn( "tess.verts not 16 byte aligned" );
		}

		// init function tables
		for ( i = 0; i < FUNCTABLE_SIZE; i++ )
		{
			tr.sinTable[ i ] = sinf( DEG2RAD( i * 360.0f / ( ( float )( FUNCTABLE_SIZE - 1 ) ) ) );
			tr.squareTable[ i ] = ( i < FUNCTABLE_SIZE / 2 ) ? 1.0f : -1.0f;
			tr.sawToothTable[ i ] = ( float ) i / FUNCTABLE_SIZE;
			tr.inverseSawToothTable[ i ] = 1.0f - tr.sawToothTable[ i ];

			if ( i < FUNCTABLE_SIZE / 2 )
			{
				if ( i < FUNCTABLE_SIZE / 4 )
				{
					tr.triangleTable[ i ] = ( float ) i / ( FUNCTABLE_SIZE / 4 );
				}
				else
				{
					tr.triangleTable[ i ] = 1.0f - tr.triangleTable[ i - FUNCTABLE_SIZE / 4 ];
				}
			}
			else
			{
				tr.triangleTable[ i ] = -tr.triangleTable[ i - FUNCTABLE_SIZE / 2 ];
			}
		}

		R_InitFogTable();

		R_NoiseInit();

		R_Register();

		if ( !InitOpenGL() )
		{
			return false;
		}

		tr.lightMode = lightMode_t( r_lightMode.Get() );

		if ( !Com_AreCheatsAllowed() )
		{
			switch( tr.lightMode )
			{
				case lightMode_t::VERTEX:
				case lightMode_t::MAP:
					break;

				default:
					tr.lightMode = lightMode_t::MAP;
					r_lightMode.Set( Util::ordinal( tr.lightMode ) );
					Cvar::Latch( r_lightMode );
					break;
			}
		}

		if ( r_showNormalMaps->integer
			|| r_showMaterialMaps->integer )
		{
			tr.lightMode = lightMode_t::MAP;
		}
		else if ( r_showLightMaps->integer
			|| r_showDeluxeMaps->integer )
		{
			tr.lightMode = lightMode_t::MAP;
		}
		else if ( r_showVertexColors.Get() )
		{
			tr.lightMode = lightMode_t::VERTEX;
		}

		backEndData[ 0 ] = ( backEndData_t * ) ri.Hunk_Alloc( sizeof( *backEndData[ 0 ] ), ha_pref::h_low );
		backEndData[ 0 ]->polys = ( srfPoly_t * ) ri.Hunk_Alloc( r_maxPolys->integer * sizeof( srfPoly_t ), ha_pref::h_low );
		backEndData[ 0 ]->polyVerts = ( polyVert_t * ) ri.Hunk_Alloc( r_maxPolyVerts->integer * sizeof( polyVert_t ), ha_pref::h_low );
		backEndData[ 0 ]->polyIndexes = ( int * ) ri.Hunk_Alloc( r_maxPolyVerts->integer * sizeof( int ), ha_pref::h_low );

		if ( r_smp->integer )
		{
			backEndData[ 1 ] = ( backEndData_t * ) ri.Hunk_Alloc( sizeof( *backEndData[ 1 ] ), ha_pref::h_low );
			backEndData[ 1 ]->polys = ( srfPoly_t * ) ri.Hunk_Alloc( r_maxPolys->integer * sizeof( srfPoly_t ), ha_pref::h_low );
			backEndData[ 1 ]->polyVerts = ( polyVert_t * ) ri.Hunk_Alloc( r_maxPolyVerts->integer * sizeof( polyVert_t ), ha_pref::h_low );
			backEndData[ 1 ]->polyIndexes = ( int * ) ri.Hunk_Alloc( r_maxPolyVerts->integer * sizeof( int ), ha_pref::h_low );
		}
		else
		{
			backEndData[ 1 ] = nullptr;
		}

		R_ToggleSmpFrame();

		R_InitImages();

		R_InitFBOs();

		R_InitVBOs();

		R_InitShaders();

		R_InitSkins();

		R_ModelInit();

		R_InitAnimations();

		R_InitFreeType();

		if ( glConfig2.textureAnisotropyAvailable )
		{
			AssertCvarRange( r_ext_texture_filter_anisotropic, 0, glConfig2.maxTextureAnisotropy, false );
		}

		R_InitVisTests();

		GL_CheckErrors();

		// print info
		GfxInfo_f();

		Log::Debug("----- finished R_Init -----" );

		return true;
	}

	/*
	===============
	RE_Shutdown
	===============
	*/
	void RE_Shutdown( bool destroyWindow )
	{
		Log::Debug("RE_Shutdown( destroyWindow = %i )", destroyWindow );

		ri.Cmd_RemoveCommand( "modellist" );
		ri.Cmd_RemoveCommand( "imagelist" );
		ri.Cmd_RemoveCommand( "shaderlist" );
		ri.Cmd_RemoveCommand( "shaderexp" );
		ri.Cmd_RemoveCommand( "skinlist" );
		ri.Cmd_RemoveCommand( "gfxinfo" );
		ri.Cmd_RemoveCommand( "modelist" );
		ri.Cmd_RemoveCommand( "shaderstate" );
		ri.Cmd_RemoveCommand( "animationlist" );
		ri.Cmd_RemoveCommand( "fbolist" );
		ri.Cmd_RemoveCommand( "vbolist" );
		ri.Cmd_RemoveCommand( "generatemtr" );
		ri.Cmd_RemoveCommand( "buildcubemaps" );

		ri.Cmd_RemoveCommand( "glsl_restart" );

		if ( tr.registered )
		{
			R_SyncRenderThread();

			CIN_CloseAllVideos();
			R_ShutdownBackend();
			R_ShutdownImages();
			R_ShutdownVBOs();
			R_ShutdownFBOs();
			R_ShutdownVisTests();
		}

		R_DoneFreeType();

		// shut down platform specific OpenGL stuff
		if ( destroyWindow )
		{
			GLSL_ShutdownGPUShaders();
			if( glConfig2.glCoreProfile ) {
				glBindVertexArray( 0 );
				glDeleteVertexArrays( 1, &backEnd.currentVAO );
			}

			GLimp_Shutdown();
			ri.Tag_Free();
		}

		tr.registered = false;
	}

	/*
	=============
	RE_EndRegistration

	Touch all images to make sure they are resident
	=============
	*/
	void RE_EndRegistration()
	{
		R_SyncRenderThread();
		if ( r_lazyShaders->integer == 1 )
		{
			GLSL_FinishGPUShaders();
		}
	}

	/*
	=====================
	GetRefAPI
	=====================
	*/
	refexport_t *GetRefAPI( int apiVersion, refimport_t *rimp )
	{
		static refexport_t re;

		ri = *rimp;

		Log::Debug("GetRefAPI()" );

		memset( &re, 0, sizeof( re ) );

		if ( apiVersion != REF_API_VERSION )
		{
			Log::Notice("Mismatched REF_API_VERSION: expected %i, got %i", REF_API_VERSION, apiVersion );
			return nullptr;
		}

		// the RE_ functions are Renderer Entry points

		// Q3A BEGIN
		re.Shutdown = RE_Shutdown;

		re.BeginRegistration = RE_BeginRegistration;
		re.RegisterModel = RE_RegisterModel;

		re.RegisterSkin = RE_RegisterSkin;
		re.RegisterShader = RE_RegisterShader;

		re.LoadWorld = RE_LoadWorldMap;
		re.SetWorldVisData = RE_SetWorldVisData;
		re.EndRegistration = RE_EndRegistration;

		re.BeginFrame = RE_BeginFrame;
		re.EndFrame = RE_EndFrame;

		re.MarkFragments = R_MarkFragments;

		re.LerpTag = RE_LerpTagET;

		re.ModelBounds = R_ModelBounds;

		re.ClearScene = RE_ClearScene;
		re.AddRefEntityToScene = RE_AddRefEntityToScene;

		re.AddPolyToScene = RE_AddPolyToSceneET;
		re.AddPolysToScene = RE_AddPolysToScene;
		re.LightForPoint = R_LightForPoint;

		re.AddLightToScene = RE_AddDynamicLightToSceneET;
		re.AddAdditiveLightToScene = RE_AddDynamicLightToSceneQ3A;

		re.RenderScene = RE_RenderScene;

		re.SetColor = RE_SetColor;
		re.SetClipRegion = RE_SetClipRegion;
		re.DrawStretchPic = RE_StretchPic;

		re.DrawRotatedPic = RE_RotatedPic;
		re.Add2dPolys = RE_2DPolyies;
		re.ScissorEnable = RE_ScissorEnable;
		re.ScissorSet = RE_ScissorSet;
		re.DrawStretchPicGradient = RE_StretchPicGradient;
		re.SetMatrixTransform = RE_SetMatrixTransform;
		re.ResetMatrixTransform = RE_ResetMatrixTransform;

		re.Glyph = RE_Glyph;
		re.GlyphChar = RE_GlyphChar;
		re.RegisterFont = RE_RegisterFont;
		re.UnregisterFont = RE_UnregisterFont;

		re.RemapShader = R_RemapShader;
		re.GetEntityToken = R_GetEntityToken;
		re.inPVS = R_inPVS;
		re.inPVVS = R_inPVVS;
		// Q3A END

		// ET BEGIN
		re.ProjectDecal = RE_ProjectDecal;
		re.ClearDecals = RE_ClearDecals;

		re.LoadDynamicShader = RE_LoadDynamicShader;
		re.Finish = RE_Finish;
		// ET END

		// XreaL BEGIN
		re.TakeVideoFrame = RE_TakeVideoFrame;

		re.AddRefLightToScene = RE_AddRefLightToScene;

		re.RegisterAnimation = RE_RegisterAnimation;
		re.CheckSkeleton = RE_CheckSkeleton;
		re.BuildSkeleton = RE_BuildSkeleton;
		re.BlendSkeleton = RE_BlendSkeleton;
		re.BoneIndex = RE_BoneIndex;
		re.AnimNumFrames = RE_AnimNumFrames;
		re.AnimFrameRate = RE_AnimFrameRate;

		// XreaL END

		re.RegisterVisTest = RE_RegisterVisTest;
		re.AddVisTestToScene = RE_AddVisTestToScene;
		re.CheckVisibility = RE_CheckVisibility;
		re.UnregisterVisTest = RE_UnregisterVisTest;

		re.SetColorGrading = RE_SetColorGrading;

		re.SetAltShaderTokens = R_SetAltShaderTokens;

		re.GetTextureSize = RE_GetTextureSize;
		re.Add2dPolysIndexed = RE_2DPolyiesIndexed;
		re.GenerateTexture = RE_GenerateTexture;
		re.ShaderNameFromHandle = RE_GetShaderNameFromHandle;
		re.SendBotDebugDrawCommands = RE_SendBotDebugDrawCommands;

		return &re;
	}
