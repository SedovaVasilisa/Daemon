/*
===========================================================================
Copyright (C) 2010-2011 Robert Beckebans <trebor_7@users.sourceforge.net>

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

#ifndef GL_SHADER_H
#define GL_SHADER_H

#include "tr_local.h"
#include <stdexcept>

#define LOG_GLSL_UNIFORMS 1
#define USE_UNIFORM_FIREWALL 1

// *INDENT-OFF*
static const unsigned int MAX_SHADER_MACROS = 10;
static const unsigned int GL_SHADER_VERSION = 5;

class ShaderException : public std::runtime_error
{
public:
	ShaderException(const char* msg) : std::runtime_error(msg) { }
};

enum class ShaderKind
{
	Unknown,
	BuiltIn,
	External
};

// Header for saved shader binaries
struct GLBinaryHeader
{
	unsigned int version;
	unsigned int checkSum; // checksum of shader source this was built from
	unsigned int driverVersionHash; // detect if the graphics driver was different

	unsigned int macros[ MAX_SHADER_MACROS ]; // macros the shader uses ( may or may not be enabled )
	unsigned int numMacros;

	GLenum binaryFormat; // argument to glProgramBinary
	uint32_t binaryLength; // argument to glProgramBinary
};

class GLUniform;
class GLUniformBlock;
class GLCompileMacro;
class GLShaderManager;

// represents a piece of GLSL code that can be copied verbatim into
// GLShaders, like a .h file in C++
class GLHeader
{
private:

	std::string                    _name;
	std::string                    _text;
	GLShaderManager               *_shaderManager;

public:
	GLHeader() : _name(), _text(), _shaderManager(nullptr)
	{}

	GLHeader( const std::string &name, const std::string &text, GLShaderManager *manager ) :
		_name( name ),
		_text(text),
		_shaderManager( manager )
	{}

	const std::string &getName() const { return _name; }
	const std::string &getText() const { return _text; }
	const GLShaderManager *getManager() const { return _shaderManager; }
};

class GLShader
{
	friend class GLShaderManager;
private:
	GLShader( const GLShader & ) = delete;
	GLShader &operator             = ( const GLShader & ) = delete;

	std::string                    _name;
	std::string                    _mainShaderName;
protected:
	int                            _activeMacros;
	unsigned int                   _checkSum;
	shaderProgram_t                 *_currentProgram;
	const uint32_t                 _vertexAttribsRequired;
	uint32_t                       _vertexAttribs; // can be set by uniforms
	GLShaderManager                 *_shaderManager;
	size_t                         _uniformStorageSize;

	std::string                    _fragmentShaderText;
	std::string                    _vertexShaderText;
	std::vector< shaderProgram_t > _shaderPrograms;


	std::vector< GLUniform * >      _uniforms;
	std::vector< GLUniformBlock * > _uniformBlocks;
	std::vector< GLCompileMacro * > _compileMacros;





	GLShader( const std::string &name, uint32_t vertexAttribsRequired, GLShaderManager *manager ) :
		_name( name ),
		_mainShaderName( name ),
		_activeMacros( 0 ),
		_checkSum( 0 ),
		_currentProgram( nullptr ),
		_vertexAttribsRequired( vertexAttribsRequired ),
		_vertexAttribs( 0 ),
		_shaderManager( manager ),
		_uniformStorageSize( 0 )
	{
	}

	GLShader( const std::string &name, const std::string &mainShaderName, uint32_t vertexAttribsRequired, GLShaderManager *manager ) :
		_name( name ),
		_mainShaderName( mainShaderName ),
		_activeMacros( 0 ),
		_checkSum( 0 ),
		_currentProgram( nullptr ),
		_vertexAttribsRequired( vertexAttribsRequired ),
		_vertexAttribs( 0 ),
		_shaderManager( manager ),
		_uniformStorageSize( 0 )
	{
	}

public:
	virtual ~GLShader()
	{
		for ( std::size_t i = 0; i < _shaderPrograms.size(); i++ )
		{
			shaderProgram_t *p = &_shaderPrograms[ i ];

			if ( p->program )
			{
				glDeleteProgram( p->program );
			}

			if ( p->VS )
			{
				glDeleteShader( p->VS );
			}

			if ( p->FS )
			{
				glDeleteShader( p->FS );
			}

			if ( p->uniformFirewall )
			{
				ri.Free( p->uniformFirewall );
			}

			if ( p->uniformLocations )
			{
				ri.Free( p->uniformLocations );
			}

			if ( p->uniformBlockIndexes )
			{
				ri.Free( p->uniformBlockIndexes );
			}
		}
	}

	void RegisterUniform( GLUniform *uniform )
	{
		_uniforms.push_back( uniform );
	}

	void RegisterUniformBlock( GLUniformBlock *uniformBlock )
	{
		_uniformBlocks.push_back( uniformBlock );
	}

	void RegisterCompileMacro( GLCompileMacro *compileMacro )
	{
		if ( _compileMacros.size() >= MAX_SHADER_MACROS )
		{
			Sys::Drop( "Can't register more than %u compile macros for a single shader", MAX_SHADER_MACROS );
		}

		_compileMacros.push_back( compileMacro );
	}

	size_t GetNumOfCompiledMacros() const
	{
		return _compileMacros.size();
	}

	shaderProgram_t        *GetProgram() const
	{
		return _currentProgram;
	}

	const std::string      &GetName() const
	{
		return _name;
	}

	const std::string      &GetMainShaderName() const
	{
		return _mainShaderName;
	}

protected:
	bool         GetCompileMacrosString( size_t permutation, std::string &compileMacrosOut ) const;
	virtual void BuildShaderVertexLibNames( std::string& /*vertexInlines*/ ) { };
	virtual void BuildShaderFragmentLibNames( std::string& /*vertexInlines*/ ) { };
	virtual void BuildShaderCompileMacros( std::string& /*vertexInlines*/ ) { };
	virtual void SetShaderProgramUniforms( shaderProgram_t* /*shaderProgram*/ ) { };
	int          SelectProgram();
public:
	void BindProgram( int deformIndex );
	void SetRequiredVertexPointers();

	bool IsMacroSet( int bit )
	{
		return ( _activeMacros & bit ) != 0;
	}

	void AddMacroBit( int bit )
	{
		_activeMacros |= bit;
	}

	void DelMacroBit( int bit )
	{
		_activeMacros &= ~bit;
	}

	bool IsVertexAttribSet( int bit )
	{
		return ( _vertexAttribs & bit ) != 0;
	}

	void AddVertexAttribBit( int bit )
	{
		_vertexAttribs |= bit;
	}

	void DelVertexAttribBit( int bit )
	{
		_vertexAttribs &= ~bit;
	}
};

class GLShaderManager
{
	std::queue< GLShader* > _shaderBuildQueue;
	std::vector< std::unique_ptr< GLShader > > _shaders;
	std::unordered_map< std::string, int > _deformShaderLookup;
	std::vector< GLint > _deformShaders;
	int       _totalBuildTime;
	unsigned int _driverVersionHash; // For cache invalidation if hardware changes
	bool _shaderBinaryCacheInvalidated;

public:
	GLHeader GLVersionDeclaration;
	GLHeader GLCompatHeader;
	GLHeader GLVertexHeader;
	GLHeader GLFragmentHeader;
	GLHeader GLEngineConstants;

	GLShaderManager() : _totalBuildTime( 0 )
	{
	}
	~GLShaderManager();

	void InitDriverInfo();

	void GenerateBuiltinHeaders();

	template< class T >
	void load( T *& shader )
	{
		if( _deformShaders.size() == 0 ) {
			Q_UNUSED(getDeformShaderIndex( nullptr, 0 ));
		}

		shader = new T( this );
		InitShader( shader );
		_shaders.emplace_back( shader );
		_shaderBuildQueue.push( shader );
	}
	void freeAll();

	int getDeformShaderIndex( deformStage_t *deforms, int numDeforms );

	void buildPermutation( GLShader *shader, int macroIndex, int deformIndex );
	void buildAll();
private:
	bool LoadShaderBinary( GLShader *shader, size_t permutation );
	void SaveShaderBinary( GLShader *shader, size_t permutation );
	GLuint CompileShader( Str::StringRef programName, Str::StringRef shaderText,
			      std::initializer_list<const GLHeader *> headers,
			      GLenum shaderType ) const;
	void CompileGPUShaders( GLShader *shader, shaderProgram_t *program,
				const std::string &compileMacros );
	void CompileAndLinkGPUShaderProgram( GLShader *shader, shaderProgram_t *program,
	                                     Str::StringRef compileMacros, int deformIndex );
	std::string BuildDeformShaderText( const std::string& steps );
	std::string BuildGPUShaderText( Str::StringRef mainShader, Str::StringRef libShaders, GLenum shaderType ) const;
	void LinkProgram( GLuint program ) const;
	void BindAttribLocations( GLuint program ) const;
	void PrintShaderSource( Str::StringRef programName, GLuint object ) const;
	void PrintInfoLog( GLuint object ) const;
	void InitShader( GLShader *shader );
	void UpdateShaderProgramUniformLocations( GLShader *shader, shaderProgram_t *shaderProgram ) const;
};

class GLUniform
{
protected:
	GLShader   *_shader;
	std::string _name;
	size_t      _firewallIndex;
	size_t      _locationIndex;

	GLUniform( GLShader *shader, const char *name ) :
		_shader( shader ),
		_name( name ),
		_firewallIndex( 0 ),
		_locationIndex( 0 )
	{
		_shader->RegisterUniform( this );
	}

public:
	virtual ~GLUniform() = default;

	void SetFirewallIndex( size_t offSetValue )
	{
		_firewallIndex = offSetValue;
	}

	void SetLocationIndex( size_t index )
	{
		_locationIndex = index;
	}

	const char *GetName()
	{
		return _name.c_str();
	}

	void UpdateShaderProgramUniformLocation( shaderProgram_t *shaderProgram )
	{
		shaderProgram->uniformLocations[ _locationIndex ] = glGetUniformLocation( shaderProgram->program, GetName() );
	}

	virtual size_t GetSize()
	{
		return 0;
	}
};

class GLUniform1i : protected GLUniform
{
protected:
	GLUniform1i( GLShader *shader, const char *name ) :
	GLUniform( shader, name )
	{
	}

	inline void SetValue( int value )
	{
		shaderProgram_t *p = _shader->GetProgram();

		ASSERT_EQ(p, glState.currentProgram);

#if defined( LOG_GLSL_UNIFORMS )
		if ( r_logFile->integer )
		{
			GLimp_LogComment( va( "GLSL_SetUniform1i( %s, shader: %s, value: %d ) ---\n",
				this->GetName(), _shader->GetName().c_str(), value ) );
		}
#endif
#if defined( USE_UNIFORM_FIREWALL )
		int *firewall = ( int * ) &p->uniformFirewall[ _firewallIndex ];

		if ( *firewall == value )
		{
			return;
		}

		*firewall = value;
#endif
		glUniform1i( p->uniformLocations[ _locationIndex ], value );
	}
public:
	size_t GetSize() override
	{
		return sizeof( int );
	}
};

class GLUniform1f : protected GLUniform
{
protected:
	GLUniform1f( GLShader *shader, const char *name ) :
	GLUniform( shader, name )
	{
	}

	inline void SetValue( float value )
	{
		shaderProgram_t *p = _shader->GetProgram();

		ASSERT_EQ(p, glState.currentProgram);

#if defined( LOG_GLSL_UNIFORMS )
		if ( r_logFile->integer )
		{
			GLimp_LogComment( va( "GLSL_SetUniform1f( %s, shader: %s, value: %f ) ---\n",
				this->GetName(), _shader->GetName().c_str(), value ) );
		}
#endif
#if defined( USE_UNIFORM_FIREWALL )
		float *firewall = ( float * ) &p->uniformFirewall[ _firewallIndex ];

		if ( *firewall == value )
		{
			return;
		}

		*firewall = value;
#endif
		glUniform1f( p->uniformLocations[ _locationIndex ], value );
	}
public:
	size_t GetSize() override
	{
		return sizeof( float );
	}
};

class GLUniform1fv : protected GLUniform
{
protected:
	GLUniform1fv( GLShader *shader, const char *name ) :
	GLUniform( shader, name )
	{
	}

	inline void SetValue( int numFloats, float *f )
	{
		shaderProgram_t *p = _shader->GetProgram();

		ASSERT_EQ(p, glState.currentProgram);

#if defined( LOG_GLSL_UNIFORMS )
		if ( r_logFile->integer )
		{
			GLimp_LogComment( va( "GLSL_SetUniform1fv( %s, shader: %s, numFloats: %d ) ---\n",
				this->GetName(), _shader->GetName().c_str(), numFloats ) );
		}
#endif
		glUniform1fv( p->uniformLocations[ _locationIndex ], numFloats, f );
	}
};

class GLUniform2f : protected GLUniform
{
protected:
	GLUniform2f( GLShader *shader, const char *name ) :
	GLUniform( shader, name )
	{
	}

	inline void SetValue( const vec2_t v )
	{
		shaderProgram_t *p = _shader->GetProgram();

		ASSERT_EQ(p, glState.currentProgram);

#if defined( LOG_GLSL_UNIFORMS )
		if ( r_logFile->integer )
		{
			GLimp_LogComment( va( "GLSL_SetUniform2f( %s, shader: %s, value: [ %f, %f ] ) ---\n",
				this->GetName(), _shader->GetName().c_str(), v[ 0 ], v[ 1 ] ) );
		}
#endif
#if defined( USE_UNIFORM_FIREWALL )
		vec2_t *firewall = ( vec2_t * ) &p->uniformFirewall[ _firewallIndex ];

		if ( ( *firewall )[ 0 ] == v[ 0 ] && ( *firewall )[ 1 ] == v[ 1 ] )
		{
			return;
		}

		( *firewall )[ 0 ] = v[ 0 ];
		( *firewall )[ 1 ] = v[ 1 ];
#endif
		glUniform2f( p->uniformLocations[ _locationIndex ], v[ 0 ], v[ 1 ] );
	}

	size_t GetSize() override
	{
		return sizeof( vec2_t );
	}
};

class GLUniform3f : protected GLUniform
{
protected:
	GLUniform3f( GLShader *shader, const char *name ) :
	GLUniform( shader, name )
	{
	}

	inline void SetValue( const vec3_t v )
	{
		shaderProgram_t *p = _shader->GetProgram();

		ASSERT_EQ(p, glState.currentProgram);

#if defined( LOG_GLSL_UNIFORMS )
		if ( r_logFile->integer )
		{
			GLimp_LogComment( va( "GLSL_SetUniform3f( %s, shader: %s, value: [ %f, %f, %f ] ) ---\n",
				this->GetName(), _shader->GetName().c_str(), v[ 0 ], v[ 1 ], v[ 2 ] ) );
		}
#endif
#if defined( USE_UNIFORM_FIREWALL )
		vec3_t *firewall = ( vec3_t * ) &p->uniformFirewall[ _firewallIndex ];

		if ( VectorCompare( *firewall, v ) )
		{
			return;
		}

		VectorCopy( v, *firewall );
#endif
		glUniform3f( p->uniformLocations[ _locationIndex ], v[ 0 ], v[ 1 ], v[ 2 ] );
	}
public:
	size_t GetSize() override
	{
		return sizeof( vec3_t );
	}
};

class GLUniform4f : protected GLUniform
{
protected:
	GLUniform4f( GLShader *shader, const char *name ) :
	GLUniform( shader, name )
	{
	}

	inline void SetValue( const vec4_t v )
	{
		shaderProgram_t *p = _shader->GetProgram();

		ASSERT_EQ(p, glState.currentProgram);

#if defined( LOG_GLSL_UNIFORMS )
		if ( r_logFile->integer )
		{
			GLimp_LogComment( va( "GLSL_SetUniform4f( %s, shader: %s, value: [ %f, %f, %f, %f ] ) ---\n",
				this->GetName(), _shader->GetName().c_str(), v[ 0 ], v[ 1 ], v[ 2 ], v[ 3 ] ) );
		}
#endif
#if defined( USE_UNIFORM_FIREWALL )
		vec4_t *firewall = ( vec4_t * ) &p->uniformFirewall[ _firewallIndex ];

		if ( !memcmp( *firewall, v, sizeof( *firewall ) ) )
		{
			return;
		}

		Vector4Copy( v, *firewall );
#endif
		glUniform4f( p->uniformLocations[ _locationIndex ], v[ 0 ], v[ 1 ], v[ 2 ], v[ 3 ] );
	}
public:
	size_t GetSize() override
	{
		return sizeof( vec4_t );
	}
};

class GLUniform4fv : protected GLUniform
{
protected:
	GLUniform4fv( GLShader *shader, const char *name ) :
	GLUniform( shader, name )
	{
	}

	inline void SetValue( int numV, vec4_t *v )
	{
		shaderProgram_t *p = _shader->GetProgram();

		ASSERT_EQ(p, glState.currentProgram);

#if defined( LOG_GLSL_UNIFORMS )
		if ( r_logFile->integer )
		{
			GLimp_LogComment( va( "GLSL_SetUniform4fv( %s, shader: %s, numV: %d ) ---\n",
				this->GetName(), _shader->GetName().c_str(), numV ) );
		}
#endif
		glUniform4fv( p->uniformLocations[ _locationIndex ], numV, &v[ 0 ][ 0 ] );
	}
};

class GLUniformMatrix4f : protected GLUniform
{
protected:
	GLUniformMatrix4f( GLShader *shader, const char *name ) :
	GLUniform( shader, name )
	{
	}

	inline void SetValue( GLboolean transpose, const matrix_t m )
	{
		shaderProgram_t *p = _shader->GetProgram();

		ASSERT_EQ(p, glState.currentProgram);

#if defined( LOG_GLSL_UNIFORMS )
		if ( r_logFile->integer )
		{
			GLimp_LogComment( va( "GLSL_SetUniformMatrix4f( %s, shader: %s, transpose: %d, [ %f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f ] ) ---\n",
				this->GetName(), _shader->GetName().c_str(), transpose,
				m[ 0 ], m[ 1 ], m[ 2 ], m[ 3 ], m[ 4 ], m[ 5 ], m[ 6 ], m[ 7 ], m[ 8 ], m[ 9 ], m[ 10 ], m[ 11 ], m[ 12 ],
				m[ 13 ], m[ 14 ], m[ 15 ] ) );
		}
#endif
#if defined( USE_UNIFORM_FIREWALL )
		matrix_t *firewall = ( matrix_t * ) &p->uniformFirewall[ _firewallIndex ];

		if ( MatrixCompare( m, *firewall ) )
		{
			return;
		}

		MatrixCopy( m, *firewall );
#endif
		glUniformMatrix4fv( p->uniformLocations[ _locationIndex ], 1, transpose, m );
	}
public:
	size_t GetSize() override
	{
		return sizeof( matrix_t );
	}
};

class GLUniformMatrix4fv : protected GLUniform
{
protected:
	GLUniformMatrix4fv( GLShader *shader, const char *name ) :
	GLUniform( shader, name )
	{
	}

	inline void SetValue( int numMatrices, GLboolean transpose, const matrix_t *m )
	{
		shaderProgram_t *p = _shader->GetProgram();

		ASSERT_EQ(p, glState.currentProgram);

#if defined( LOG_GLSL_UNIFORMS )
		if ( r_logFile->integer )
		{
			GLimp_LogComment( va( "GLSL_SetUniformMatrix4fv( %s, shader: %s, numMatrices: %d, transpose: %d ) ---\n",
				this->GetName(), _shader->GetName().c_str(), numMatrices, transpose ) );
		}
#endif
		glUniformMatrix4fv( p->uniformLocations[ _locationIndex ], numMatrices, transpose, &m[ 0 ][ 0 ] );
	}
};

class GLUniformMatrix34fv : protected GLUniform
{
protected:
	GLUniformMatrix34fv( GLShader *shader, const char *name ) :
	GLUniform( shader, name )
	{
	}

	inline void SetValue( int numMatrices, GLboolean transpose, const float *m )
	{
		shaderProgram_t *p = _shader->GetProgram();

		ASSERT_EQ(p, glState.currentProgram);
#if defined( LOG_GLSL_UNIFORMS )
		if ( r_logFile->integer )
		{
			GLimp_LogComment( va( "GLSL_SetUniformMatrix34fv( %s, shader: %s, numMatrices: %d, transpose: %d ) ---\n",
				this->GetName(), _shader->GetName().c_str(), numMatrices, transpose ) );
		}
#endif
		glUniformMatrix3x4fv( p->uniformLocations[ _locationIndex ], numMatrices, transpose, m );
	}
};

class GLUniformBlock
{
protected:
	GLShader   *_shader;
	std::string _name;
	size_t      _locationIndex;

	GLUniformBlock( GLShader *shader, const char *name ) :
		_shader( shader ),
		_name( name ),
		_locationIndex( 0 )
	{
		_shader->RegisterUniformBlock( this );
	}

public:
	void SetLocationIndex( size_t index )
	{
		_locationIndex = index;
	}

	const char *GetName()
	{
		return _name.c_str();
	}

	void UpdateShaderProgramUniformBlockIndex( shaderProgram_t *shaderProgram )
	{
		shaderProgram->uniformBlockIndexes[ _locationIndex ] = glGetUniformBlockIndex( shaderProgram->program, GetName() );
	}

	void SetBuffer( GLuint buffer ) {
		shaderProgram_t *p = _shader->GetProgram();
		GLuint blockIndex = p->uniformBlockIndexes[ _locationIndex ];

		ASSERT_EQ(p, glState.currentProgram);

		if( blockIndex != GL_INVALID_INDEX ) {
			glBindBufferBase( GL_UNIFORM_BUFFER, blockIndex, buffer );
		}
	}
};

class GLCompileMacro
{
private:
	int     _bit;

protected:
	GLShader *_shader;

	GLCompileMacro( GLShader *shader ) :
		_shader( shader )
	{
		_bit = BIT( _shader->GetNumOfCompiledMacros() );
		_shader->RegisterCompileMacro( this );
	}

// RB: This is not good oo design, but it can be a workaround and its cost is more or less only a virtual function call.
// It also works regardless of RTTI is enabled or not.
	enum EGLCompileMacro : unsigned
	{
	  USE_BSP_SURFACE,
	  USE_VERTEX_SKINNING,
	  USE_VERTEX_ANIMATION,
	  USE_VERTEX_SPRITE,
	  USE_TCGEN_ENVIRONMENT,
	  USE_TCGEN_LIGHTMAP,
	  USE_DELUXE_MAPPING,
	  USE_GRID_LIGHTING,
	  USE_GRID_DELUXE_MAPPING,
	  USE_HEIGHTMAP_IN_NORMALMAP,
	  USE_RELIEF_MAPPING,
	  USE_REFLECTIVE_SPECULAR,
	  USE_SHADOWING,
	  LIGHT_DIRECTIONAL,
	  USE_DEPTH_FADE,
	  USE_PHYSICAL_MAPPING,
	  USE_ALPHA_TESTING
	};

public:
	virtual const char       *GetName() const = 0;
	virtual EGLCompileMacro GetType() const = 0;

	virtual bool            HasConflictingMacros( size_t, const std::vector<GLCompileMacro*>& ) const
	{
		return false;
	}

	virtual bool            MissesRequiredMacros( size_t, const std::vector<GLCompileMacro*>& ) const
	{
		return false;
	}

	virtual uint32_t        GetRequiredVertexAttributes() const
	{
		return 0;
	}

	void SetMacro( bool enable )
	{
		int bit = GetBit();

		if ( enable && !_shader->IsMacroSet( bit ) )
		{
			_shader->AddMacroBit( bit );
		}
		else if ( !enable && _shader->IsMacroSet( bit ) )
		{
			_shader->DelMacroBit( bit );
		}
		// else do nothing because already enabled/disabled
	}

public:
	int GetBit() const
	{
		return _bit;
	}

	virtual ~GLCompileMacro() = default;
};

class GLCompileMacro_USE_BSP_SURFACE :
	GLCompileMacro
{
public:
	GLCompileMacro_USE_BSP_SURFACE( GLShader *shader ) :
		GLCompileMacro( shader )
	{
	}

	const char *GetName() const override
	{
		return "USE_BSP_SURFACE";
	}

	bool HasConflictingMacros(size_t permutation, const std::vector< GLCompileMacro * > &macros) const override;

	EGLCompileMacro GetType() const override
	{
		return EGLCompileMacro::USE_BSP_SURFACE;
	}

	void SetBspSurface( bool enable )
	{
		SetMacro( enable );
	}
};

class GLCompileMacro_USE_VERTEX_SKINNING :
	GLCompileMacro
{
public:
	GLCompileMacro_USE_VERTEX_SKINNING( GLShader *shader ) :
		GLCompileMacro( shader )
	{
	}

	const char *GetName() const override
	{
		return "USE_VERTEX_SKINNING";
	}

	EGLCompileMacro GetType() const override
	{
		return EGLCompileMacro::USE_VERTEX_SKINNING;
	}

	bool HasConflictingMacros( size_t permutation, const std::vector< GLCompileMacro * > &macros ) const override;
	bool MissesRequiredMacros( size_t permutation, const std::vector< GLCompileMacro * > &macros ) const override;

	uint32_t        GetRequiredVertexAttributes() const override
	{
		return ATTR_BONE_FACTORS;
	}

	void SetVertexSkinning( bool enable )
	{
		SetMacro( enable );
	}
};

class GLCompileMacro_USE_VERTEX_ANIMATION :
	GLCompileMacro
{
public:
	GLCompileMacro_USE_VERTEX_ANIMATION( GLShader *shader ) :
		GLCompileMacro( shader )
	{
	}

	const char *GetName() const override
	{
		return "USE_VERTEX_ANIMATION";
	}

	EGLCompileMacro GetType() const override
	{
		return EGLCompileMacro::USE_VERTEX_ANIMATION;
	}

	bool     HasConflictingMacros( size_t permutation, const std::vector< GLCompileMacro * > &macros ) const override;
	uint32_t GetRequiredVertexAttributes() const override;

	void SetVertexAnimation( bool enable )
	{
		SetMacro( enable );
	}
};

class GLCompileMacro_USE_VERTEX_SPRITE :
	GLCompileMacro
{
public:
	GLCompileMacro_USE_VERTEX_SPRITE( GLShader *shader ) :
		GLCompileMacro( shader )
	{
	}

	const char *GetName() const override
	{
		return "USE_VERTEX_SPRITE";
	}

	EGLCompileMacro GetType() const override
	{
		return EGLCompileMacro::USE_VERTEX_SPRITE;
	}

	bool     HasConflictingMacros( size_t permutation, const std::vector< GLCompileMacro * > &macros ) const override;
	uint32_t GetRequiredVertexAttributes() const override
	{
		return ATTR_QTANGENT;
	}

	void SetVertexSprite( bool enable )
	{
		SetMacro( enable );
	}
};

class GLCompileMacro_USE_TCGEN_ENVIRONMENT :
	GLCompileMacro
{
public:
	GLCompileMacro_USE_TCGEN_ENVIRONMENT( GLShader *shader ) :
		GLCompileMacro( shader )
	{
	}

	const char *GetName() const override
	{
		return "USE_TCGEN_ENVIRONMENT";
	}

	EGLCompileMacro GetType() const override
	{
		return EGLCompileMacro::USE_TCGEN_ENVIRONMENT;
	}

	bool     HasConflictingMacros(size_t permutation, const std::vector< GLCompileMacro * > &macros) const override;
	uint32_t        GetRequiredVertexAttributes() const override
	{
		return ATTR_QTANGENT;
	}

	void SetTCGenEnvironment( bool enable )
	{
		SetMacro( enable );
	}
};

class GLCompileMacro_USE_TCGEN_LIGHTMAP :
	GLCompileMacro
{
public:
	GLCompileMacro_USE_TCGEN_LIGHTMAP( GLShader *shader ) :
		GLCompileMacro( shader )
	{
	}

	const char *GetName() const override
	{
		return "USE_TCGEN_LIGHTMAP";
	}

	bool     HasConflictingMacros(size_t permutation, const std::vector< GLCompileMacro * > &macros) const override;
	EGLCompileMacro GetType() const override
	{
		return EGLCompileMacro::USE_TCGEN_LIGHTMAP;
	}

	void SetTCGenLightmap( bool enable )
	{
		SetMacro( enable );
	}
};

class GLCompileMacro_USE_GRID_LIGHTING :
	GLCompileMacro
{
public:
	GLCompileMacro_USE_GRID_LIGHTING( GLShader *shader ) :
		GLCompileMacro( shader )
	{
	}

	const char *GetName() const override
	{
		return "USE_GRID_LIGHTING";
	}

	bool HasConflictingMacros(size_t permutation, const std::vector< GLCompileMacro * > &macros) const override;

	EGLCompileMacro GetType() const override
	{
		return EGLCompileMacro::USE_GRID_LIGHTING;
	}

	void SetGridLighting( bool enable )
	{
		SetMacro( enable );
	}
};

class GLCompileMacro_USE_DELUXE_MAPPING :
	GLCompileMacro
{
public:
	GLCompileMacro_USE_DELUXE_MAPPING( GLShader *shader ) :
		GLCompileMacro( shader )
	{
	}

	const char *GetName() const override
	{
		return "USE_DELUXE_MAPPING";
	}

	bool HasConflictingMacros(size_t permutation, const std::vector< GLCompileMacro * > &macros) const override;

	EGLCompileMacro GetType() const override
	{
		return EGLCompileMacro::USE_DELUXE_MAPPING;
	}

	void SetDeluxeMapping( bool enable )
	{
		SetMacro( enable );
	}
};

class GLCompileMacro_USE_GRID_DELUXE_MAPPING :
	GLCompileMacro
{
public:
	GLCompileMacro_USE_GRID_DELUXE_MAPPING( GLShader *shader ) :
		GLCompileMacro( shader )
	{
	}

	const char *GetName() const override
	{
		return "USE_GRID_DELUXE_MAPPING";
	}

	bool HasConflictingMacros(size_t permutation, const std::vector< GLCompileMacro * > &macros) const override;

	EGLCompileMacro GetType() const override
	{
		return EGLCompileMacro::USE_GRID_DELUXE_MAPPING;
	}

	void SetGridDeluxeMapping( bool enable )
	{
		SetMacro( enable );
	}
};

class GLCompileMacro_USE_HEIGHTMAP_IN_NORMALMAP :
	GLCompileMacro
{
public:
	GLCompileMacro_USE_HEIGHTMAP_IN_NORMALMAP( GLShader *shader ) :
		GLCompileMacro( shader )
	{
	}

	const char *GetName() const override
	{
		return "USE_HEIGHTMAP_IN_NORMALMAP";
	}

	EGLCompileMacro GetType() const override
	{
		return EGLCompileMacro::USE_HEIGHTMAP_IN_NORMALMAP;
	}

	void SetHeightMapInNormalMap( bool enable )
	{
		SetMacro( enable );
	}
};

class GLCompileMacro_USE_RELIEF_MAPPING :
	GLCompileMacro
{
public:
	GLCompileMacro_USE_RELIEF_MAPPING( GLShader *shader ) :
		GLCompileMacro( shader )
	{
	}

	const char *GetName() const override
	{
		return "USE_RELIEF_MAPPING";
	}

	EGLCompileMacro GetType() const override
	{
		return EGLCompileMacro::USE_RELIEF_MAPPING;
	}

	void SetReliefMapping( bool enable )
	{
		SetMacro( enable );
	}
};

class GLCompileMacro_USE_REFLECTIVE_SPECULAR :
	GLCompileMacro
{
public:
	GLCompileMacro_USE_REFLECTIVE_SPECULAR( GLShader *shader ) :
		GLCompileMacro( shader )
	{
	}

	const char *GetName() const override
	{
		return "USE_REFLECTIVE_SPECULAR";
	}

	bool HasConflictingMacros(size_t permutation, const std::vector< GLCompileMacro * > &macros) const override;

	EGLCompileMacro GetType() const override
	{
		return EGLCompileMacro::USE_REFLECTIVE_SPECULAR;
	}

	void SetReflectiveSpecular( bool enable )
	{
		SetMacro( enable );
	}
};

class GLCompileMacro_LIGHT_DIRECTIONAL :
	GLCompileMacro
{
public:
	GLCompileMacro_LIGHT_DIRECTIONAL( GLShader *shader ) :
		GLCompileMacro( shader )
	{
	}

	const char *GetName() const override
	{
		return "LIGHT_DIRECTIONAL";
	}

	EGLCompileMacro GetType() const override
	{
		return EGLCompileMacro::LIGHT_DIRECTIONAL;
	}

	void SetMacro_LIGHT_DIRECTIONAL( bool enable )
	{
		SetMacro( enable );
	}
};

class GLCompileMacro_USE_SHADOWING :
	GLCompileMacro
{
public:
	GLCompileMacro_USE_SHADOWING( GLShader *shader ) :
		GLCompileMacro( shader )
	{
	}

	const char *GetName() const override
	{
		return "USE_SHADOWING";
	}

	EGLCompileMacro GetType() const override
	{
		return EGLCompileMacro::USE_SHADOWING;
	}

	void SetShadowing( bool enable )
	{
		SetMacro( enable );
	}
};

class GLCompileMacro_USE_DEPTH_FADE :
	GLCompileMacro
{
public:
	GLCompileMacro_USE_DEPTH_FADE( GLShader *shader ) :
		GLCompileMacro( shader )
	{
	}

	const char *GetName() const override
	{
		return "USE_DEPTH_FADE";
	}

	bool HasConflictingMacros(size_t permutation, const std::vector<GLCompileMacro*> &macros) const override;
	EGLCompileMacro GetType() const override
	{
		return EGLCompileMacro::USE_DEPTH_FADE;
	}

	void SetDepthFade( bool enable )
	{
		SetMacro( enable );
	}
};

class GLCompileMacro_USE_PHYSICAL_MAPPING :
	GLCompileMacro
{
public:
	GLCompileMacro_USE_PHYSICAL_MAPPING( GLShader *shader ) :
		GLCompileMacro( shader )
	{
	}

	const char *GetName() const override
	{
		return "USE_PHYSICAL_MAPPING";
	}

	EGLCompileMacro GetType() const override
	{
		return USE_PHYSICAL_MAPPING;
	}

	void SetPhysicalShading( bool enable )
	{
		SetMacro( enable );
	}
};

class GLCompileMacro_USE_ALPHA_TESTING :
	GLCompileMacro
{
public:
	GLCompileMacro_USE_ALPHA_TESTING(GLShader *shader) :
		GLCompileMacro(shader)
	{
	}

	const char *GetName() const override
	{
		return "USE_ALPHA_TESTING";
	}

	EGLCompileMacro GetType() const override
	{
		return USE_ALPHA_TESTING;
	}

	void SetAlphaTesting(bool enable)
	{
		SetMacro( enable );
	}
};

class u_TextureMatrix :
	GLUniformMatrix4f
{
public:
	u_TextureMatrix( GLShader *shader ) :
		GLUniformMatrix4f( shader, "u_TextureMatrix" )
	{
	}

	void SetUniform_TextureMatrix( const matrix_t m )
	{
		this->SetValue( GL_FALSE, m );
	}
};

class u_AlphaThreshold :
	GLUniform1f
{
public:
	u_AlphaThreshold( GLShader *shader ) :
		GLUniform1f( shader, "u_AlphaThreshold" )
	{
	}

	void SetUniform_AlphaTest( uint32_t stateBits )
	{
		float value;

		switch( stateBits & GLS_ATEST_BITS ) {
			case GLS_ATEST_GT_0:
				if ( *r_dpBlend )
				{
					// DarkPlaces only supports one alphaFunc operation:
					//   https://gitlab.com/xonotic/darkplaces/blob/324a5329d33ef90df59e6488abce6433d90ac04c/model_shared.c#L1875-1876
					// Which is GE128:
					//   https://gitlab.com/xonotic/darkplaces/blob/0ea8f691e05ea968bb8940942197fa627966ff99/render.h#L95
					// Because of that, people may silently introduce regressions in their textures
					// designed for GT0 by compressing them using a lossy picture format like Jpg.
					// Xonotic texture known to trigger this bug:
					//   models/desertfactory/textures/shaders/grass01
					// Using GE128 instead would hide Jpeg artifacts while not breaking that much
					// non-DarkPlaces GT0.
					// No one operation other than GT0 an GE128 was found in whole Xonotic corpus,
					// so if there is other operations used in third party maps, they were broken
					// on DarkPlaces and will work there.
					value = 0.5f;
				}
				else
				{
					value = 1.0f;
				}
				break;
			case GLS_ATEST_LT_128:
				value = -1.5f;
				break;
			case GLS_ATEST_GE_128:
				value = 0.5f;
				break;
			case GLS_ATEST_GT_ENT:
				value = 1.0f - backEnd.currentEntity->e.shaderRGBA.Alpha() * ( 1.0f / 255.0f );
				break;
			case GLS_ATEST_LT_ENT:
				value = -2.0f + backEnd.currentEntity->e.shaderRGBA.Alpha() * ( 1.0f / 255.0f );
				break;
			default:
				value = 1.5f;
				break;
		}

		this->SetValue( value );
	}
};

class u_ViewOrigin :
	GLUniform3f
{
public:
	u_ViewOrigin( GLShader *shader ) :
		GLUniform3f( shader, "u_ViewOrigin" )
	{
	}

	void SetUniform_ViewOrigin( const vec3_t v )
	{
		this->SetValue( v );
	}
};

class u_ViewUp :
	GLUniform3f
{
public:
	u_ViewUp( GLShader *shader ) :
		GLUniform3f( shader, "u_ViewUp" )
	{
	}

	void SetUniform_ViewUp( const vec3_t v )
	{
		this->SetValue( v );
	}
};

class u_LightDir :
	GLUniform3f
{
public:
	u_LightDir( GLShader *shader ) :
		GLUniform3f( shader, "u_LightDir" )
	{
	}

	void SetUniform_LightDir( const vec3_t v )
	{
		this->SetValue( v );
	}
};

class u_LightOrigin :
	GLUniform3f
{
public:
	u_LightOrigin( GLShader *shader ) :
		GLUniform3f( shader, "u_LightOrigin" )
	{
	}

	void SetUniform_LightOrigin( const vec3_t v )
	{
		this->SetValue( v );
	}
};

class u_LightColor :
	GLUniform3f
{
public:
	u_LightColor( GLShader *shader ) :
		GLUniform3f( shader, "u_LightColor" )
	{
	}

	void SetUniform_LightColor( const vec3_t v )
	{
		this->SetValue( v );
	}
};

class u_LightRadius :
	GLUniform1f
{
public:
	u_LightRadius( GLShader *shader ) :
		GLUniform1f( shader, "u_LightRadius" )
	{
	}

	void SetUniform_LightRadius( float value )
	{
		this->SetValue( value );
	}
};

class u_LightScale :
	GLUniform1f
{
public:
	u_LightScale( GLShader *shader ) :
		GLUniform1f( shader, "u_LightScale" )
	{
	}

	void SetUniform_LightScale( float value )
	{
		this->SetValue( value );
	}
};

class u_LightAttenuationMatrix :
	GLUniformMatrix4f
{
public:
	u_LightAttenuationMatrix( GLShader *shader ) :
		GLUniformMatrix4f( shader, "u_LightAttenuationMatrix" )
	{
	}

	void SetUniform_LightAttenuationMatrix( const matrix_t m )
	{
		this->SetValue( GL_FALSE, m );
	}
};

class u_LightFrustum :
	GLUniform4fv
{
public:
	u_LightFrustum( GLShader *shader ) :
		GLUniform4fv( shader, "u_LightFrustum" )
	{
	}

	void SetUniform_LightFrustum( vec4_t lightFrustum[ 6 ] )
	{
		this->SetValue( 6, lightFrustum );
	}
};

class u_ShadowTexelSize :
	GLUniform1f
{
public:
	u_ShadowTexelSize( GLShader *shader ) :
		GLUniform1f( shader, "u_ShadowTexelSize" )
	{
	}

	void SetUniform_ShadowTexelSize( float value )
	{
		this->SetValue( value );
	}
};

class u_ShadowBlur :
	GLUniform1f
{
public:
	u_ShadowBlur( GLShader *shader ) :
		GLUniform1f( shader, "u_ShadowBlur" )
	{
	}

	void SetUniform_ShadowBlur( float value )
	{
		this->SetValue( value );
	}
};

class u_ShadowMatrix :
	GLUniformMatrix4fv
{
public:
	u_ShadowMatrix( GLShader *shader ) :
		GLUniformMatrix4fv( shader, "u_ShadowMatrix" )
	{
	}

	void SetUniform_ShadowMatrix( matrix_t m[ MAX_SHADOWMAPS ] )
	{
		this->SetValue( MAX_SHADOWMAPS, GL_FALSE, m );
	}
};

class u_ShadowParallelSplitDistances :
	GLUniform4f
{
public:
	u_ShadowParallelSplitDistances( GLShader *shader ) :
		GLUniform4f( shader, "u_ShadowParallelSplitDistances" )
	{
	}

	void SetUniform_ShadowParallelSplitDistances( const vec4_t v )
	{
		this->SetValue( v );
	}
};

class u_RefractionIndex :
	GLUniform1f
{
public:
	u_RefractionIndex( GLShader *shader ) :
		GLUniform1f( shader, "u_RefractionIndex" )
	{
	}

	void SetUniform_RefractionIndex( float value )
	{
		this->SetValue( value );
	}
};

class u_FresnelPower :
	GLUniform1f
{
public:
	u_FresnelPower( GLShader *shader ) :
		GLUniform1f( shader, "u_FresnelPower" )
	{
	}

	void SetUniform_FresnelPower( float value )
	{
		this->SetValue( value );
	}
};

class u_FresnelScale :
	GLUniform1f
{
public:
	u_FresnelScale( GLShader *shader ) :
		GLUniform1f( shader, "u_FresnelScale" )
	{
	}

	void SetUniform_FresnelScale( float value )
	{
		this->SetValue( value );
	}
};

class u_FresnelBias :
	GLUniform1f
{
public:
	u_FresnelBias( GLShader *shader ) :
		GLUniform1f( shader, "u_FresnelBias" )
	{
	}

	void SetUniform_FresnelBias( float value )
	{
		this->SetValue( value );
	}
};

class u_NormalScale :
	GLUniform3f
{
public:
	u_NormalScale( GLShader *shader ) :
		GLUniform3f( shader, "u_NormalScale" )
	{
	}

	void SetUniform_NormalScale( const vec3_t value )
	{
		this->SetValue( value );
	}
};


class u_FogDensity :
	GLUniform1f
{
public:
	u_FogDensity( GLShader *shader ) :
		GLUniform1f( shader, "u_FogDensity" )
	{
	}

	void SetUniform_FogDensity( float value )
	{
		this->SetValue( value );
	}
};

class u_FogColor :
	GLUniform3f
{
public:
	u_FogColor( GLShader *shader ) :
		GLUniform3f( shader, "u_FogColor" )
	{
	}

	void SetUniform_FogColor( const vec3_t v )
	{
		this->SetValue( v );
	}
};

class u_Color :
	GLUniform4f
{
public:
	u_Color( GLShader *shader ) :
		GLUniform4f( shader, "u_Color" )
	{
	}

	void SetUniform_Color( const Color::Color& color )
	{
		this->SetValue( color.ToArray() );
	}
};

class u_ModelMatrix :
	GLUniformMatrix4f
{
public:
	u_ModelMatrix( GLShader *shader ) :
		GLUniformMatrix4f( shader, "u_ModelMatrix" )
	{
	}

	void SetUniform_ModelMatrix( const matrix_t m )
	{
		this->SetValue( GL_FALSE, m );
	}
};

class u_ViewMatrix :
	GLUniformMatrix4f
{
public:
	u_ViewMatrix( GLShader *shader ) :
		GLUniformMatrix4f( shader, "u_ViewMatrix" )
	{
	}

	void SetUniform_ViewMatrix( const matrix_t m )
	{
		this->SetValue( GL_FALSE, m );
	}
};

class u_ModelViewMatrix :
	GLUniformMatrix4f
{
public:
	u_ModelViewMatrix( GLShader *shader ) :
		GLUniformMatrix4f( shader, "u_ModelViewMatrix" )
	{
	}

	void SetUniform_ModelViewMatrix( const matrix_t m )
	{
		this->SetValue( GL_FALSE, m );
	}
};

class u_ModelViewMatrixTranspose :
	GLUniformMatrix4f
{
public:
	u_ModelViewMatrixTranspose( GLShader *shader ) :
		GLUniformMatrix4f( shader, "u_ModelViewMatrixTranspose" )
	{
	}

	void SetUniform_ModelViewMatrixTranspose( const matrix_t m )
	{
		this->SetValue( GL_TRUE, m );
	}
};

class u_ProjectionMatrixTranspose :
	GLUniformMatrix4f
{
public:
	u_ProjectionMatrixTranspose( GLShader *shader ) :
		GLUniformMatrix4f( shader, "u_ProjectionMatrixTranspose" )
	{
	}

	void SetUniform_ProjectionMatrixTranspose( const matrix_t m )
	{
		this->SetValue( GL_TRUE, m );
	}
};

class u_ModelViewProjectionMatrix :
	GLUniformMatrix4f
{
public:
	u_ModelViewProjectionMatrix( GLShader *shader ) :
		GLUniformMatrix4f( shader, "u_ModelViewProjectionMatrix" )
	{
	}

	void SetUniform_ModelViewProjectionMatrix( const matrix_t m )
	{
		this->SetValue( GL_FALSE, m );
	}
};

class u_UnprojectMatrix :
	GLUniformMatrix4f
{
public:
	u_UnprojectMatrix( GLShader *shader ) :
		GLUniformMatrix4f( shader, "u_UnprojectMatrix" )
	{
	}

	void SetUniform_UnprojectMatrix( const matrix_t m )
	{
		this->SetValue( GL_FALSE, m );
	}
};

class u_Bones :
	GLUniform4fv
{
public:
	u_Bones( GLShader *shader ) :
		GLUniform4fv( shader, "u_Bones" )
	{
	}

	void SetUniform_Bones( int numBones, transform_t bones[ MAX_BONES ] )
	{
		this->SetValue( 2 * numBones, &bones[ 0 ].rot );
	}
};

class u_VertexInterpolation :
	GLUniform1f
{
public:
	u_VertexInterpolation( GLShader *shader ) :
		GLUniform1f( shader, "u_VertexInterpolation" )
	{
	}

	void SetUniform_VertexInterpolation( float value )
	{
		this->SetValue( value );
	}
};

class u_PortalRange :
	GLUniform1f
{
public:
	u_PortalRange( GLShader *shader ) :
		GLUniform1f( shader, "u_PortalRange" )
	{
	}

	void SetUniform_PortalRange( float value )
	{
		this->SetValue( value );
	}
};

class u_DepthScale :
	GLUniform1f
{
public:
	u_DepthScale( GLShader *shader ) :
		GLUniform1f( shader, "u_DepthScale" )
	{
	}

	void SetUniform_DepthScale( float value )
	{
		this->SetValue( value );
	}
};

class u_ReliefDepthScale :
	GLUniform1f
{
public:
	u_ReliefDepthScale( GLShader *shader ) :
		GLUniform1f( shader, "u_ReliefDepthScale" )
	{
	}

	void SetUniform_ReliefDepthScale( float value )
	{
		this->SetValue( value );
	}
};

class u_ReliefOffsetBias :
	GLUniform1f
{
public:
	u_ReliefOffsetBias( GLShader *shader ) :
		GLUniform1f( shader, "u_ReliefOffsetBias" )
	{
	}

	void SetUniform_ReliefOffsetBias( float value )
	{
		this->SetValue( value );
	}
};

class u_EnvironmentInterpolation :
	GLUniform1f
{
public:
	u_EnvironmentInterpolation( GLShader *shader ) :
		GLUniform1f( shader, "u_EnvironmentInterpolation" )
	{
	}

	void SetUniform_EnvironmentInterpolation( float value )
	{
		this->SetValue( value );
	}
};

class u_Time :
	GLUniform1f
{
public:
	u_Time( GLShader *shader ) :
		GLUniform1f( shader, "u_Time" )
	{
	}

	void SetUniform_Time( float value )
	{
		this->SetValue( value );
	}
};

class GLDeformStage :
	public u_Time
{
public:
	GLDeformStage( GLShader *shader ) :
		u_Time( shader )
	{
	}
};

class u_ColorModulate :
	GLUniform4f
{
public:
	u_ColorModulate( GLShader *shader ) :
		GLUniform4f( shader, "u_ColorModulate" )
	{
	}

	void SetUniform_ColorModulate( vec4_t v )
	{
		this->SetValue( v );
	}
	void SetUniform_ColorModulate( colorGen_t colorGen, alphaGen_t alphaGen )
	{
		vec4_t v;
		bool needAttrib = false;

		if ( r_logFile->integer )
		{
			GLimp_LogComment( va( "--- u_ColorModulate::SetUniform_ColorModulate( program = %s, colorGen = %s, alphaGen = %s ) ---\n", _shader->GetName().c_str(), Util::enum_str(colorGen), Util::enum_str(alphaGen)) );
		}

		switch ( colorGen )
		{
			case colorGen_t::CGEN_VERTEX:
				needAttrib = true;
				VectorSet( v, 1, 1, 1 );
				break;

			case colorGen_t::CGEN_ONE_MINUS_VERTEX:
				needAttrib = true;
				VectorSet( v, -1, -1, -1 );
				break;

			default:
				VectorSet( v, 0, 0, 0 );
				break;
		}

		switch ( alphaGen )
		{
			case alphaGen_t::AGEN_VERTEX:
				needAttrib = true;
				v[ 3 ] = 1.0f;
				break;

			case alphaGen_t::AGEN_ONE_MINUS_VERTEX:
				needAttrib = true;
				v[ 3 ] = -1.0f;
				break;

			default:
				v[ 3 ] = 0.0f;
				break;
		}

		if ( needAttrib )
		{
			_shader->AddVertexAttribBit( ATTR_COLOR );
		}
		else
		{
			_shader->DelVertexAttribBit( ATTR_COLOR );
		}
		this->SetValue( v );
	}
};

class u_FogDistanceVector :
	GLUniform4f
{
public:
	u_FogDistanceVector( GLShader *shader ) :
		GLUniform4f( shader, "u_FogDistanceVector" )
	{
	}

	void SetUniform_FogDistanceVector( const vec4_t v )
	{
		this->SetValue( v );
	}
};

class u_FogDepthVector :
	GLUniform4f
{
public:
	u_FogDepthVector( GLShader *shader ) :
		GLUniform4f( shader, "u_FogDepthVector" )
	{
	}

	void SetUniform_FogDepthVector( const vec4_t v )
	{
		this->SetValue( v );
	}
};

class u_FogEyeT :
	GLUniform1f
{
public:
	u_FogEyeT( GLShader *shader ) :
		GLUniform1f( shader, "u_FogEyeT" )
	{
	}

	void SetUniform_FogEyeT( float value )
	{
		this->SetValue( value );
	}
};

class u_DeformMagnitude :
	GLUniform1f
{
public:
	u_DeformMagnitude( GLShader *shader ) :
		GLUniform1f( shader, "u_DeformMagnitude" )
	{
	}

	void SetUniform_DeformMagnitude( float value )
	{
		this->SetValue( value );
	}
};

class u_blurVec :
	GLUniform3f
{
public:
	u_blurVec( GLShader *shader ) :
		GLUniform3f( shader, "u_blurVec" )
	{
	}

	void SetUniform_blurVec( vec3_t value )
	{
		this->SetValue( value );
	}
};

class u_TexScale :
	GLUniform2f
{
public:
	u_TexScale( GLShader *shader ) :
		GLUniform2f( shader, "u_TexScale" )
	{
	}

	void SetUniform_TexScale( vec2_t value )
	{
		this->SetValue( value );
	}
};

class u_SpecularExponent :
	GLUniform2f
{
public:
	u_SpecularExponent( GLShader *shader ) :
		GLUniform2f( shader, "u_SpecularExponent" )
	{
	}

	void SetUniform_SpecularExponent( float min, float max )
	{
		// in the fragment shader, the exponent must be computed as
		// exp = ( max - min ) * gloss + min
		// to expand the [0...1] range of gloss to [min...max]
		// we precompute ( max - min ) before sending the uniform to the fragment shader
		vec2_t v = { max - min, min };
		this->SetValue( v );
	}
};

class u_InverseGamma :
	GLUniform1f
{
public:
	u_InverseGamma( GLShader *shader ) :
		GLUniform1f( shader, "u_InverseGamma" )
	{
	}

	void SetUniform_InverseGamma( float value )
	{
		this->SetValue( value );
	}
};

class u_LightGridOrigin :
	GLUniform3f
{
public:
	u_LightGridOrigin( GLShader *shader ) :
		GLUniform3f( shader, "u_LightGridOrigin" )
	{
	}

	void SetUniform_LightGridOrigin( vec3_t origin )
	{
		this->SetValue( origin );
	}
};

class u_LightGridScale :
	GLUniform3f
{
public:
	u_LightGridScale( GLShader *shader ) :
		GLUniform3f( shader, "u_LightGridScale" )
	{
	}

	void SetUniform_LightGridScale( vec3_t scale )
	{
		this->SetValue( scale );
	}
};

class u_zFar :
	GLUniform3f
{
public:
	u_zFar( GLShader *shader ) :
		GLUniform3f( shader, "u_zFar" )
	{
	}

	void SetUniform_zFar( const vec3_t value )
	{
		this->SetValue( value );
	}
};

class u_numLights :
	GLUniform1i
{
public:
	u_numLights( GLShader *shader ) :
		GLUniform1i( shader, "u_numLights" )
	{
	}

	void SetUniform_numLights( int value )
	{
		this->SetValue( value );
	}
};

class u_lightLayer :
	GLUniform1i
{
public:
	u_lightLayer( GLShader *shader ) :
		GLUniform1i( shader, "u_lightLayer" )
	{
	}

	void SetUniform_lightLayer( int value )
	{
		this->SetValue( value );
	}
};

class u_Lights :
	GLUniformBlock
{
 public:
	u_Lights( GLShader *shader ) :
		GLUniformBlock( shader, "u_Lights" )
	{
	}

	void SetUniformBlock_Lights( GLuint buffer )
	{
		this->SetBuffer( buffer );
	}
};

// This is just a copy of the GLShader_generic, but with a special
// define for RmlUI transforms. It probably has a lot of unnecessary
// code that could be pruned.
// TODO: Write a more minimal 2D rendering shader.
class GLShader_generic2D :
	public GLShader,
	public u_TextureMatrix,
	public u_AlphaThreshold,
	public u_ModelMatrix,
	public u_ModelViewProjectionMatrix,
	public u_ColorModulate,
	public u_Color,
	public u_DepthScale,
	public GLDeformStage,
	public GLCompileMacro_USE_DEPTH_FADE,
	public GLCompileMacro_USE_ALPHA_TESTING
{
public:
	GLShader_generic2D( GLShaderManager *manager );
	void BuildShaderVertexLibNames( std::string& vertexInlines ) override;
	void BuildShaderCompileMacros( std::string& compileMacros ) override;
	void SetShaderProgramUniforms( shaderProgram_t *shaderProgram ) override;
};

class GLShader_generic :
	public GLShader,
	public u_TextureMatrix,
	public u_ViewOrigin,
	public u_ViewUp,
	public u_AlphaThreshold,
	public u_ModelMatrix,
 	public u_ProjectionMatrixTranspose,
	public u_ModelViewProjectionMatrix,
	public u_ColorModulate,
	public u_Color,
	public u_Bones,
	public u_VertexInterpolation,
	public u_DepthScale,
	public GLDeformStage,
	public GLCompileMacro_USE_VERTEX_SKINNING,
	public GLCompileMacro_USE_VERTEX_ANIMATION,
	public GLCompileMacro_USE_VERTEX_SPRITE,
	public GLCompileMacro_USE_TCGEN_ENVIRONMENT,
	public GLCompileMacro_USE_TCGEN_LIGHTMAP,
	public GLCompileMacro_USE_DEPTH_FADE,
	public GLCompileMacro_USE_ALPHA_TESTING
{
public:
	GLShader_generic( GLShaderManager *manager );
	void BuildShaderVertexLibNames( std::string& vertexInlines ) override;
	void SetShaderProgramUniforms( shaderProgram_t *shaderProgram ) override;
};

class GLShader_lightMapping :
	public GLShader,
	public u_TextureMatrix,
	public u_SpecularExponent,
	public u_ColorModulate,
	public u_Color,
	public u_AlphaThreshold,
	public u_ViewOrigin,
	public u_ModelMatrix,
	public u_ModelViewProjectionMatrix,
	public u_Bones,
	public u_VertexInterpolation,
	public u_ReliefDepthScale,
	public u_ReliefOffsetBias,
	public u_NormalScale,
	public u_EnvironmentInterpolation,
	public u_LightGridOrigin,
	public u_LightGridScale,
	public u_numLights,
	public u_Lights,
	public GLDeformStage,
	public GLCompileMacro_USE_BSP_SURFACE,
	public GLCompileMacro_USE_VERTEX_SKINNING,
	public GLCompileMacro_USE_VERTEX_ANIMATION,
	public GLCompileMacro_USE_DELUXE_MAPPING,
	public GLCompileMacro_USE_GRID_LIGHTING,
	public GLCompileMacro_USE_GRID_DELUXE_MAPPING,
	public GLCompileMacro_USE_HEIGHTMAP_IN_NORMALMAP,
	public GLCompileMacro_USE_RELIEF_MAPPING,
	public GLCompileMacro_USE_REFLECTIVE_SPECULAR,
	public GLCompileMacro_USE_PHYSICAL_MAPPING
{
public:
	GLShader_lightMapping( GLShaderManager *manager );
	void BuildShaderVertexLibNames( std::string& vertexInlines ) override;
	void BuildShaderFragmentLibNames( std::string& fragmentInlines ) override;
	void BuildShaderCompileMacros( std::string& compileMacros ) override;
	void SetShaderProgramUniforms( shaderProgram_t *shaderProgram ) override;
};

class GLShader_forwardLighting_omniXYZ :
	public GLShader,
	public u_TextureMatrix,
	public u_SpecularExponent,
	public u_AlphaThreshold,
	public u_ColorModulate,
	public u_Color,
	public u_ViewOrigin,
	public u_LightOrigin,
	public u_LightColor,
	public u_LightRadius,
	public u_LightScale,
	public u_LightAttenuationMatrix,
	public u_ShadowTexelSize,
	public u_ShadowBlur,
	public u_ModelMatrix,
	public u_ModelViewProjectionMatrix,
	public u_Bones,
	public u_VertexInterpolation,
	public u_ReliefDepthScale,
	public u_ReliefOffsetBias,
	public u_NormalScale,
	public GLDeformStage,
	public GLCompileMacro_USE_VERTEX_SKINNING,
	public GLCompileMacro_USE_VERTEX_ANIMATION,
	public GLCompileMacro_USE_HEIGHTMAP_IN_NORMALMAP,
	public GLCompileMacro_USE_RELIEF_MAPPING,
	public GLCompileMacro_USE_SHADOWING //,
{
public:
	GLShader_forwardLighting_omniXYZ( GLShaderManager *manager );
	void BuildShaderVertexLibNames( std::string& vertexInlines ) override;
	void BuildShaderFragmentLibNames( std::string& fragmentInlines ) override;
	void BuildShaderCompileMacros( std::string& compileMacros ) override;
	void SetShaderProgramUniforms( shaderProgram_t *shaderProgram ) override;
};

class GLShader_forwardLighting_projXYZ :
	public GLShader,
	public u_TextureMatrix,
	public u_SpecularExponent,
	public u_AlphaThreshold,
	public u_ColorModulate,
	public u_Color,
	public u_ViewOrigin,
	public u_LightOrigin,
	public u_LightColor,
	public u_LightRadius,
	public u_LightScale,
	public u_LightAttenuationMatrix,
	public u_ShadowTexelSize,
	public u_ShadowBlur,
	public u_ShadowMatrix,
	public u_ModelMatrix,
	public u_ModelViewProjectionMatrix,
	public u_Bones,
	public u_VertexInterpolation,
	public u_ReliefDepthScale,
	public u_ReliefOffsetBias,
	public u_NormalScale,
	public GLDeformStage,
	public GLCompileMacro_USE_VERTEX_SKINNING,
	public GLCompileMacro_USE_VERTEX_ANIMATION,
	public GLCompileMacro_USE_HEIGHTMAP_IN_NORMALMAP,
	public GLCompileMacro_USE_RELIEF_MAPPING,
	public GLCompileMacro_USE_SHADOWING //,
{
public:
	GLShader_forwardLighting_projXYZ( GLShaderManager *manager );
	void BuildShaderVertexLibNames( std::string& vertexInlines ) override;
	void BuildShaderFragmentLibNames( std::string& fragmentInlines ) override;
	void BuildShaderCompileMacros( std::string& compileMacros ) override;
	void SetShaderProgramUniforms( shaderProgram_t *shaderProgram ) override;
};

class GLShader_forwardLighting_directionalSun :
	public GLShader,
	public u_TextureMatrix,
	public u_SpecularExponent,
	public u_AlphaThreshold,
	public u_ColorModulate,
	public u_Color,
	public u_ViewOrigin,
	public u_LightDir,
	public u_LightColor,
	public u_LightRadius,
	public u_LightScale,
	public u_LightAttenuationMatrix,
	public u_ShadowTexelSize,
	public u_ShadowBlur,
	public u_ShadowMatrix,
	public u_ShadowParallelSplitDistances,
	public u_ModelMatrix,
	public u_ViewMatrix,
	public u_ModelViewProjectionMatrix,
	public u_Bones,
	public u_VertexInterpolation,
	public u_ReliefDepthScale,
	public u_ReliefOffsetBias,
	public u_NormalScale,
	public GLDeformStage,
	public GLCompileMacro_USE_VERTEX_SKINNING,
	public GLCompileMacro_USE_VERTEX_ANIMATION,
	public GLCompileMacro_USE_HEIGHTMAP_IN_NORMALMAP,
	public GLCompileMacro_USE_RELIEF_MAPPING,
	public GLCompileMacro_USE_SHADOWING //,
{
public:
	GLShader_forwardLighting_directionalSun( GLShaderManager *manager );
	void BuildShaderVertexLibNames( std::string& vertexInlines ) override;
	void BuildShaderFragmentLibNames( std::string& fragmentInlines ) override;
	void BuildShaderCompileMacros( std::string& compileMacros ) override;
	void SetShaderProgramUniforms( shaderProgram_t *shaderProgram ) override;
};

class GLShader_shadowFill :
	public GLShader,
	public u_TextureMatrix,
	public u_ViewOrigin,
	public u_AlphaThreshold,
	public u_LightOrigin,
	public u_LightRadius,
	public u_ModelMatrix,
	public u_ModelViewProjectionMatrix,
	public u_Color,
	public u_Bones,
	public u_VertexInterpolation,
	public GLDeformStage,
	public GLCompileMacro_USE_VERTEX_SKINNING,
	public GLCompileMacro_USE_VERTEX_ANIMATION,
	public GLCompileMacro_LIGHT_DIRECTIONAL
{
public:
	GLShader_shadowFill( GLShaderManager *manager );
	void BuildShaderVertexLibNames( std::string& vertexInlines ) override;
	void SetShaderProgramUniforms( shaderProgram_t *shaderProgram ) override;
};

class GLShader_reflection :
	public GLShader,
	public u_TextureMatrix,
	public u_ViewOrigin,
	public u_ModelMatrix,
	public u_ModelViewProjectionMatrix,
	public u_Bones,
	public u_ReliefDepthScale,
	public u_ReliefOffsetBias,
	public u_NormalScale,
	public u_VertexInterpolation,
	public GLDeformStage,
	public GLCompileMacro_USE_VERTEX_SKINNING,
	public GLCompileMacro_USE_VERTEX_ANIMATION,
	public GLCompileMacro_USE_HEIGHTMAP_IN_NORMALMAP,
	public GLCompileMacro_USE_RELIEF_MAPPING
{
public:
	GLShader_reflection( GLShaderManager *manager );
	void BuildShaderVertexLibNames( std::string& vertexInlines ) override;
	void BuildShaderFragmentLibNames( std::string& fragmentInlines ) override;
	void BuildShaderCompileMacros( std::string& compileMacros ) override;
	void SetShaderProgramUniforms( shaderProgram_t *shaderProgram ) override;
};

class GLShader_skybox :
	public GLShader,
	public u_ViewOrigin,
	public u_ModelMatrix,
	public u_ModelViewProjectionMatrix,
	public u_Bones,
	public u_VertexInterpolation,
	public GLDeformStage
{
public:
	GLShader_skybox( GLShaderManager *manager );
	void SetShaderProgramUniforms( shaderProgram_t *shaderProgram ) override;
};

class GLShader_fogQuake3 :
	public GLShader,
	public u_ModelMatrix,
	public u_ModelViewProjectionMatrix,
	public u_Color,
	public u_Bones,
	public u_VertexInterpolation,
	public u_FogDistanceVector,
	public u_FogDepthVector,
	public u_FogEyeT,
	public GLDeformStage,
	public GLCompileMacro_USE_VERTEX_SKINNING,
	public GLCompileMacro_USE_VERTEX_ANIMATION
{
public:
	GLShader_fogQuake3( GLShaderManager *manager );
	void BuildShaderVertexLibNames( std::string& vertexInlines ) override;
	void SetShaderProgramUniforms( shaderProgram_t *shaderProgram ) override;
};

class GLShader_fogGlobal :
	public GLShader,
	public u_ViewOrigin,
	public u_ViewMatrix,
	public u_ModelViewProjectionMatrix,
	public u_UnprojectMatrix,
	public u_Color,
	public u_FogDistanceVector,
	public u_FogDepthVector
{
public:
	GLShader_fogGlobal( GLShaderManager *manager );
	void SetShaderProgramUniforms( shaderProgram_t *shaderProgram ) override;
};

class GLShader_heatHaze :
	public GLShader,
	public u_TextureMatrix,
	public u_ViewOrigin,
	public u_ViewUp,
	public u_DeformMagnitude,
	public u_ModelMatrix,
	public u_ModelViewProjectionMatrix,
	public u_ModelViewMatrixTranspose,
	public u_ProjectionMatrixTranspose,
	public u_ColorModulate,
	public u_Color,
	public u_Bones,
	public u_NormalScale,
	public u_VertexInterpolation,
	public GLDeformStage,
	public GLCompileMacro_USE_VERTEX_SKINNING,
	public GLCompileMacro_USE_VERTEX_ANIMATION,
	public GLCompileMacro_USE_VERTEX_SPRITE
{
public:
	GLShader_heatHaze( GLShaderManager *manager );
	void BuildShaderVertexLibNames( std::string& vertexInlines ) override;
	void BuildShaderFragmentLibNames( std::string& fragmentInlines ) override;
	void SetShaderProgramUniforms( shaderProgram_t *shaderProgram ) override;
};

class GLShader_screen :
	public GLShader,
	public u_ModelViewProjectionMatrix
{
public:
	GLShader_screen( GLShaderManager *manager );
	void SetShaderProgramUniforms( shaderProgram_t *shaderProgram ) override;
};

class GLShader_portal :
	public GLShader,
	public u_ModelViewMatrix,
	public u_ModelViewProjectionMatrix,
	public u_PortalRange
{
public:
	GLShader_portal( GLShaderManager *manager );
	void SetShaderProgramUniforms( shaderProgram_t *shaderProgram ) override;
};

class GLShader_contrast :
	public GLShader,
	public u_ModelViewProjectionMatrix
{
public:
	GLShader_contrast( GLShaderManager *manager );
	void SetShaderProgramUniforms( shaderProgram_t *shaderProgram ) override;
};

class GLShader_cameraEffects :
	public GLShader,
	public u_ColorModulate,
	public u_TextureMatrix,
	public u_ModelViewProjectionMatrix,
	public u_DeformMagnitude,
	public u_InverseGamma
{
public:
	GLShader_cameraEffects( GLShaderManager *manager );
	void SetShaderProgramUniforms( shaderProgram_t *shaderProgram ) override;
};

class GLShader_blurX :
	public GLShader,
	public u_ModelViewProjectionMatrix,
	public u_DeformMagnitude,
	public u_TexScale
{
public:
	GLShader_blurX( GLShaderManager *manager );
	void SetShaderProgramUniforms( shaderProgram_t *shaderProgram ) override;
};

class GLShader_blurY :
	public GLShader,
	public u_ModelViewProjectionMatrix,
	public u_DeformMagnitude,
	public u_TexScale
{
public:
	GLShader_blurY( GLShaderManager *manager );
	void SetShaderProgramUniforms( shaderProgram_t *shaderProgram ) override;
};

class GLShader_debugShadowMap :
	public GLShader,
	public u_ModelViewProjectionMatrix
{
public:
	GLShader_debugShadowMap( GLShaderManager *manager );
	void SetShaderProgramUniforms( shaderProgram_t *shaderProgram ) override;
};

class GLShader_liquid :
	public GLShader,
	public u_TextureMatrix,
	public u_ViewOrigin,
	public u_RefractionIndex,
	public u_ModelMatrix,
	public u_ModelViewProjectionMatrix,
	public u_UnprojectMatrix,
	public u_FresnelPower,
	public u_FresnelScale,
	public u_FresnelBias,
	public u_ReliefDepthScale,
	public u_ReliefOffsetBias,
	public u_NormalScale,
	public u_FogDensity,
	public u_FogColor,
	public u_SpecularExponent,
	public u_LightGridOrigin,
	public u_LightGridScale,
	public GLCompileMacro_USE_HEIGHTMAP_IN_NORMALMAP,
	public GLCompileMacro_USE_RELIEF_MAPPING
{
public:
	GLShader_liquid( GLShaderManager *manager );
	void BuildShaderFragmentLibNames( std::string& fragmentInlines ) override;
	void SetShaderProgramUniforms( shaderProgram_t *shaderProgram ) override;
};

class GLShader_motionblur :
	public GLShader,
	public u_blurVec
{
public:
	GLShader_motionblur( GLShaderManager *manager );
	void SetShaderProgramUniforms( shaderProgram_t *shaderProgram ) override;
};

class GLShader_ssao :
	public GLShader,
	public u_zFar
{
public:
	GLShader_ssao( GLShaderManager *manager );
	void SetShaderProgramUniforms( shaderProgram_t *shaderProgram ) override;
};

class GLShader_depthtile1 :
	public GLShader,
	public u_zFar
{
public:
	GLShader_depthtile1( GLShaderManager *manager );
	void SetShaderProgramUniforms( shaderProgram_t *shaderProgram ) override;
};

class GLShader_depthtile2 :
	public GLShader
{
public:
	GLShader_depthtile2( GLShaderManager *manager );
	void SetShaderProgramUniforms( shaderProgram_t *shaderProgram ) override;
};

class GLShader_lighttile :
	public GLShader,
	public u_ModelMatrix,
	public u_numLights,
	public u_lightLayer,
	public u_Lights,
	public u_zFar
{
public:
	GLShader_lighttile( GLShaderManager *manager );
	void SetShaderProgramUniforms( shaderProgram_t *shaderProgram ) override;
};

class GLShader_fxaa :
	public GLShader
{
public:
	GLShader_fxaa( GLShaderManager *manager );
	void SetShaderProgramUniforms( shaderProgram_t *shaderProgram ) override;
	void BuildShaderFragmentLibNames( std::string& fragmentInlines ) override;
};

std::string GetShaderPath();

extern ShaderKind shaderKind;

extern GLShader_generic2D                       *gl_generic2DShader;
extern GLShader_generic                         *gl_genericShader;
extern GLShader_lightMapping                    *gl_lightMappingShader;
extern GLShader_forwardLighting_omniXYZ         *gl_forwardLightingShader_omniXYZ;
extern GLShader_forwardLighting_projXYZ         *gl_forwardLightingShader_projXYZ;
extern GLShader_forwardLighting_directionalSun *gl_forwardLightingShader_directionalSun;
extern GLShader_shadowFill                      *gl_shadowFillShader;
extern GLShader_reflection                      *gl_reflectionShader;
extern GLShader_skybox                          *gl_skyboxShader;
extern GLShader_fogQuake3                       *gl_fogQuake3Shader;
extern GLShader_fogGlobal                       *gl_fogGlobalShader;
extern GLShader_heatHaze                        *gl_heatHazeShader;
extern GLShader_screen                          *gl_screenShader;
extern GLShader_portal                          *gl_portalShader;
extern GLShader_contrast                        *gl_contrastShader;
extern GLShader_cameraEffects                   *gl_cameraEffectsShader;
extern GLShader_blurX                           *gl_blurXShader;
extern GLShader_blurY                           *gl_blurYShader;
extern GLShader_debugShadowMap                  *gl_debugShadowMapShader;
extern GLShader_liquid                          *gl_liquidShader;
extern GLShader_motionblur                      *gl_motionblurShader;
extern GLShader_ssao                            *gl_ssaoShader;
extern GLShader_depthtile1                      *gl_depthtile1Shader;
extern GLShader_depthtile2                      *gl_depthtile2Shader;
extern GLShader_lighttile                       *gl_lighttileShader;
extern GLShader_fxaa                            *gl_fxaaShader;
extern GLShaderManager                           gl_shaderManager;

#endif // GL_SHADER_H
