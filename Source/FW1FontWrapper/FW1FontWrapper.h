// FW1FontWrapper.h

// v1.1, October 2011
// Written by Erik Rufelt

#ifndef IncludeGuard__FW1_FW1FontWrapper_h
#define IncludeGuard__FW1_FW1FontWrapper_h

#include <D3D11.h>
#include <DWrite.h>


/// <summary>The current FW1 version.</summary>
/// <remarks>This constant should be used when calling FW1CreateFactory to make sure the library version matches the headers.</remarks>
#define FW1_VERSION 0x110f

#define FW1_DLL_W L"FW1FontWrapper.dll"
#define FW1_DLL_A "FW1FontWrapper.dll"

#ifdef UNICODE
	#define FW1_DLL FW1_DLL_W
#else
	#define FW1_DLL FW1_DLL_A
#endif

/// <summary>Describes overrides for how an FW1 operation is performed.</summary>
/// <remarks>These flags can be used for any FW1 methods that take a flags parameter. Not all flags have any meaning for all methods however.
/// Consult the documentation page for a particular method for information on what flags are valid.</remarks>
enum FW1_TEXT_FLAG {
	/// <summary>Text is left-aligned. This is the default.</summary>
	FW1_LEFT = 0x0,
	
	/// <summary>Text is centered horizontally.</summary>
	FW1_CENTER = 0x1,
	
	/// <summary>Text is right-aligned.</summary>
	FW1_RIGHT = 0x2,
	
	/// <summary>Text is aligned at the top of the layout-box. This is the default.</summary>
	FW1_TOP = 0x0,
	
	/// <summary>Text is centered vertically.</summary>
	FW1_VCENTER = 0x4,
	
	/// <summary>Text is aligned at the bottom of the layout-box.</summary>
	FW1_BOTTOM = 0x8,
	
	/// <summary>No automatic wrapping when the text overflows the layout-box.</summary>
	FW1_NOWORDWRAP = 0x10,
	
	/// <summary>Text is drawn without anti-aliasing.</summary>
	FW1_ALIASED = 0x20,
	
	/// <summary>If a clip-rect is specified together with this flag, all text is clipped to inside the specified rectangle.</summary>
	FW1_CLIPRECT = 0x40,
	
	/// <summary>No geometry shader is used when drawing glyphs. Indexed quads are constructed on the CPU instead of in the geometry shader.</summary>
	FW1_NOGEOMETRYSHADER = 0x80,
	
	/// <summary>The transform matrix and the clip-rect is not updated in the internal constant-buffer. Can be used as an optimization when a previous call has already set the correct data.</summary>
	FW1_CONSTANTSPREPARED = 0x100,
	
	/// <summary>The internal vertex and index buffer (if used) are assumed to already be bound. Can be used as an optimization when a previous call has already set the buffers.</summary>
	FW1_BUFFERSPREPARED = 0x200,
	
	/// <summary>The correct shaders/constant-buffer etc. are assumed to already be bound. Can be used as an optimization when a previous call has already set the states, or to override the default states.</summary>
	FW1_STATEPREPARED = 0x400,
	
	/// <summary>Can be used as an optimization on subsequent calls, when drawing several strings with the same settings.</summary>
	FW1_IMMEDIATECALL = FW1_CONSTANTSPREPARED | FW1_BUFFERSPREPARED | FW1_STATEPREPARED,
	
	/// <summary>When a draw method returns, the device-context will have been restored to the same state as before the call.</summary>
	FW1_RESTORESTATE = 0x800,
	
	/// <summary>Any new glyphs added during a call are not flushed to the device-resources.
	/// It is a good idea to use this flag for text-operations on deferred contexts, when drawing text on multiple threads simultaneously, in order to guarantee the proper order of operations.</summary>
	FW1_NOFLUSH = 0x1000,
	
	/// <summary>Any new glyphs will be cached in the atlas and glyph-maps, but no geometry is drawn.</summary>
	FW1_CACHEONLY = 0x2000,
	
	/// <summary>No new glyphs will be added to the atlas or glyph-maps. Any glyphs not already present in the atlas will be replaced with a default fall-back glyph (empty box).</summary>
	FW1_NONEWGLYPHS = 0x4000,
	
	/// <summary>A text-layout will be run through DirectWrite and new fonts will be prepared, but no actual drawing will take place, and no additional glyphs will be cached.</summary>
	FW1_ANALYZEONLY = 0x8000,
	
	/// <summary>Don't use.</summary>
	FW1_UNUSED = 0xffffffff
};

/// <summary>Coordinates for a single glyph in the atlas.</summary>
/// <remarks>Each glyph image inserted in a glyph sheet texture gets a unique index in that sheet, and a corresponding FW1_GLYPHCOORDS entry in the sheet's coord buffer, describing its location in the sheet as well as its dimensions.</remarks>
struct FW1_GLYPHCOORDS {
	/// <summary>The left texture coordinate.</summary>
	FLOAT TexCoordLeft;
	
	/// <summary>The top texture coordinate.</summary>
	FLOAT TexCoordTop;
	
	/// <summary>The right texture coordinate.</summary>
	FLOAT TexCoordRight;
	
	/// <summary>The bottom texture coordinate.</summary>
	FLOAT TexCoordBottom;
	
	/// <summary>The offset of the left edge of the glyph image, relative its offset in the text.</summary>
	FLOAT PositionLeft;
	
	/// <summary>The offset of the top edge of the glyph image, relative its offset in the text.</summary>
	FLOAT PositionTop;
	
	/// <summary>The offset of the right edge of the glyph image, relative its offset in the text.</summary>
	FLOAT PositionRight;
	
	/// <summary>The offset of the bottom edge of the glyph image, relative its offset in the text.</summary>
	FLOAT PositionBottom;
};

/// <summary>Description of a glyph sheet.</summary>
/// <remarks></remarks>
struct FW1_GLYPHSHEETDESC {
	/// <summary>The number of glyphs currently stored in this sheet.</summary>
	UINT GlyphCount;
	
	/// <summary>The width of this sheet's texture, in pixels.</summary>
	UINT Width;
	
	/// <summary>The height of this sheet's texture, in pixels.</summary>
	UINT Height;
	
	/// <summary>The number of mip-levels for this sheet's texture.</summary>
	UINT MipLevels;
};

/// <summary>Metrics for a glyph image.</summary>
/// <remarks>This structure is filled in as part of the FW1_GLYPHIMAGEDATA structure when a glyph-image is rendered by IFW1DWriteRenderTarget::DrawGlyphTemp.</remarks>
struct FW1_GLYPHMETRICS {
	/// <summary>The horizontal offset from a glyph's position in text to the left edge of its image.</summary>
	FLOAT OffsetX;
	
	/// <summary>The vertical offset form a glyph's position in text to the top edge of its image.</summary>
	FLOAT OffsetY;
	
	/// <summary>The width of the glyph image, in pixels.</summary>
	UINT Width;
	
	/// <summary>The height of the glyph image, in pixels.</summary>
	UINT Height;
};

/// <summary>Image data for a glyph.</summary>
/// <remarks>This structure is filled by the IFW1DWriteRenderTarget::DrawGlyphTemp Method.</remarks>
struct FW1_GLYPHIMAGEDATA {
	/// <summary>Metrics for the glyph.</summary>
	FW1_GLYPHMETRICS Metrics;
	
	/// <summary>Pointer to the pixels of the glyph-image.</summary>
	const void *pGlyphPixels;
	
	/// <summary>The number of bytes in a row of the image data.</summary>
	UINT RowPitch;
	
	/// <summary>The number of bytes between the start of one pixel and the next.</summary>
	UINT PixelStride;
};

/// <summary>A vertex corresponding to a single glyph.</summary>
/// <remarks>When an IFW1TextRenderer draws a string, each output glyph is converted to an FW1_GLYPHVERTEX entry in an IFW1TextGeometry object.</remarks>
struct FW1_GLYPHVERTEX {
	/// <summary>The base X position of the glyph.</summary>
	FLOAT PositionX;
	
	/// <summary>The base Y position of the glyph.</summary>
	FLOAT PositionY;
	
	/// <summary>The index of the glyph.</summary>
	UINT32 GlyphIndex;
	
	/// <summary>The color of the glyph, as 0xAaBbGgRr.</summary>
	UINT32 GlyphColor;
};

/// <summary>An array of vertices, sorted by glyph-sheet.</summary>
/// <remarks>This structure is returned by the IFW1TextGeometry::GetGlyphVerticesTemp Method.</remarks>
struct FW1_VERTEXDATA {
	/// <summary>The number of sheets in the glyph-atlas that are used, starting with the first sheet in the atlas.</summary>
	UINT SheetCount;
	
	/// <summary>An array of <i>SheetCount</i> unsigned integers, which specify the number of glyphs using each sheet.
	/// The sum of all counts is <i>TotalVertexCount</i>. Some counts may be zero.</summary>
	const UINT *pVertexCounts;
	
	/// <summary>The total number of vertices.</summary>
	UINT TotalVertexCount;
	
	/// <summary>An array of <i>TotalVertexCount</i> vertices, sorted by sheet.</summary>
	const FW1_GLYPHVERTEX *pVertices;
};

/// <summary>A rectangle.</summary>
/// <remarks></remarks>
struct FW1_RECTF {
	/// <summary>The X coordinate of the left edge of the rectangle.</summary>
	FLOAT Left;
	
	/// <summary>The Y coordinate of the top edge of the rectangle.</summary>
	FLOAT Top;
	
	/// <summary>The X coordinate of the right edge of the rectangle.</summary>
	FLOAT Right;
	
	/// <summary>The Y coordinate of the bottom edge of the rectangle.</summary>
	FLOAT Bottom;
};

/// <summary>Describes a single font. This structure is used in the FW1_FONTWRAPPERCREATEPARAMS structure.</summary>
/// <remarks>If pszFontFamily is NULL when creating an IFW1FontWrapper object, no default font will be set up.
/// This is perfectly valid when drawing text using one of the DrawTextLayout methods.
/// However, the DrawString methods will silently fail if no default font is set up.<br/>
/// If pszFontFamily is not NULL, the FontWeight, FontStyle and FontStretch members must be set to valid values according to the DirectWrite documentation.
/// Zero is not a valid value for these.</remarks>
struct FW1_DWRITEFONTPARAMS {
	/// <summary>The name of the font-family. Valid values include <i>Arial</i>, <i>Courier New</i>, etc. as long as the specified font is installed.
	/// Unavailable fonts will automatically fall back to a different font.
	/// This member can be set to NULL, if no default font is desired when using the structure to create a font-wrapper.</summary>
	LPCWSTR pszFontFamily;
	
	/// <summary>The font weight. See DirectWrite documentation.</summary>
	DWRITE_FONT_WEIGHT FontWeight;
	
	/// <summary>The font style. See DirectWrite documentation.</summary>
	DWRITE_FONT_STYLE FontStyle;
	
	/// <summary>The font stretch. See DirectWrite documentation.</summary>
	DWRITE_FONT_STRETCH FontStretch;
	
	/// <summary>The locale. NULL for default.</summary>
	LPCWSTR pszLocale;
};

/// <summary>The FW1_FONTWRAPPERCREATEPARAMS is used with the IFW1Factory::CreateFontWrapper method, and describes settings for the created IFW1FontWrapper object.</summary>
/// <remarks>If a member has the value zero, the default value will be chosen instead. See FW1_DWRITEFONTPARAMS for requirements for its members.</remarks>
struct FW1_FONTWRAPPERCREATEPARAMS {
	/// <summary>The width of the glyph sheet textures to store glyph images in. 0 defaults to 512.</summary>
	UINT GlyphSheetWidth;
	
	/// <summary>The height of the glyph sheet textures to store glyph images in. 0 defaults to 512.</summary>
	UINT GlyphSheetHeight;
	
	/// <summary>The maximum number of glyphs per texture. A buffer of <i>MaxGlyphCountPerSheet * 32</i> bytes is preallocated for each sheet. 0 defaults to 2048.</summary>
	UINT MaxGlyphCountPerSheet;
	
	/// <summary>The number of mip-levels for the glyph sheet textures. 0 defaults to 1.</summary>
	UINT SheetMipLevels;
	
	/// <summary>If set to TRUE, the sampler-state is created with anisotropic filtering.</summary>
	BOOL AnisotropicFiltering;
	
	/// <summary>The maximum width of a single glyph.
	/// This value is used to decide how large the DirectWrite render target needs to be, which is used when drawing glyph images to put in the atlas.
	/// 0 defaults to 384.</summary>
	UINT MaxGlyphWidth;
	
	/// <summary>The maximum height of a single glyph.
	/// This value is used to decide how large the DirectWrite render target needs to be, which is used when drawing glyph images to put in the atlas.
	/// 0 defaults to 384.</summary>
	UINT MaxGlyphHeight;
	
	/// <summary>If set to TRUE, no geometry shader is used.</summary>
	BOOL DisableGeometryShader;
	
	/// <summary>The size in bytes of the dynamic vertex buffer to upload glyph vertices to when drawing a string. 0 defaults to 4096 * 16.<br/>
	/// Each glyph vertex is either 16 or 20 bytes in size, and each glyph requires either 1 or 4 vertices depending on if the geometry shader is used.</summary>
	UINT VertexBufferSize;
	
	/// <summary>Description of the default font. See FW1_DWRITEFONTPARAMS.</summary>
	FW1_DWRITEFONTPARAMS DefaultFontParams;
};

interface IFW1Factory;
/// <summary>All FW1 interfaces (except for IFW1Factory) inherits from IFW1Object.</summary>
/// <remarks>Since all interfaces inhert from IFW1Object, the factory which created an object can always be queried with its GetFactory method.</remarks>
MIDL_INTERFACE("8D3C3FB1-F2CC-4331-A623-031F74C06617") IFW1Object : public IUnknown {
	public:
		/// <summary>Get the factory that created an object.</summary>
		/// <remarks></remarks>
		/// <returns>Standard HRESULT error code.</returns>
		/// <param name="ppFactory">Address of a pointer to an IFW1Factory.</param>
		virtual HRESULT STDMETHODCALLTYPE GetFactory(
			__out IFW1Factory **ppFactory
		) = 0;
};

/// <summary>A sheet contains a texture with glyph images, and a coord-buffer with an FW1_GLYPHCOORDS entry for each glyph.</summary>
MIDL_INTERFACE("60CAB266-C805-461d-82C0-392472EECEFA") IFW1GlyphSheet : public IFW1Object {
	public:
		/// <summary>Get the ID3D11Device the sheet is created on.</summary>
		/// <remarks></remarks>
		/// <returns>Standard HRESULT error code.</returns>
		/// <param name="ppDevice">Address of a pointer to an ID3D11Device.</param>
		virtual HRESULT STDMETHODCALLTYPE GetDevice(
			__out ID3D11Device **ppDevice
		) = 0;
		
		/// <summary>Get the properties of a glyph sheet.</summary>
		/// <remarks></remarks>
		/// <returns>Returns nothing.</returns>
		/// <param name="pDesc">Pointer to a sheet description.</param>
		virtual void STDMETHODCALLTYPE GetDesc(
			__out FW1_GLYPHSHEETDESC *pDesc
		) = 0;
		
		/// <summary>Get the ID3D11ShaderResourceView for the sheet's texture.</summary>
		/// <remarks></remarks>
		/// <returns>Standard HRESULT error code.</returns>
		/// <param name="ppSheetTextureSRV">Address of a pointer to an ID3D11ShaderResourceView.</param>
		virtual HRESULT STDMETHODCALLTYPE GetSheetTexture(
			__out ID3D11ShaderResourceView **ppSheetTextureSRV
		) = 0;
		
		/// <summary>Get the ID3D11ShaderResourceView for the sheet's coord buffer.</summary>
		/// <remarks>The coord buffer contains 32 bytes per glyph, stored as two float4, representing the data in an FW1_GLYPHCOORDS structure.<br/>
		/// If the sheet is created without a hardware coord buffer, the method will return success and set the coord buffer to NULL.
		/// See IFW1Factory::CreateGlyphSheet.</remarks>
		/// <returns>Standard HRESULT error code.</returns>
		/// <param name="ppCoordBufferSRV">Address of a pointer to an ID3D11ShaderResourceView.</param>
		virtual HRESULT STDMETHODCALLTYPE GetCoordBuffer(
			__out ID3D11ShaderResourceView **ppCoordBufferSRV
		) = 0;
		
		/// <summary>Get a sheet's coord-buffer, as an array of FW1_GLYPHCOORDS.</summary>
		/// <remarks>The returned buffer is valid for the lifetime of the sheet, and may only be read and never altered.
		/// The maximum valid index at any given time is one less than the number of glyphs in the sheet, which can be queried using IFW1GlyphSheet::GetDesc.</remarks>
		/// <returns>Returns a constant pointer to the sheet's coord-buffer.</returns>
		virtual const FW1_GLYPHCOORDS* STDMETHODCALLTYPE GetGlyphCoords(
		) = 0;
		
		/// <summary>Set the sheet shader resources on the provided context.</summary>
		/// <remarks>This method sets the sheet texture as a pixelshader resource for slot 0, and optionally the coord buffer as geometryshader resource for slot 0.</remarks>
		/// <returns>Standard HRESULT error code.</returns>
		/// <param name="pContext">The context to set the sheet shader resources on.</param>
		/// <param name="Flags">This parameter can include zero or more of the following values, ORd together. Any additional values are ignored.<br/>
		/// FW1_NOGEOMETRYSHADER: don't bind the coord buffer as a shader-resource for the geometry shader, even if it's available.
		/// </param>
		virtual HRESULT STDMETHODCALLTYPE BindSheet(
			__in ID3D11DeviceContext *pContext,
			__in UINT Flags
		) = 0;
		
		/// <summary>Insert a glyph into the sheet.</summary>
		/// <remarks>The parameters for this method can be obtained as part of the FW1_GLYPHIMAGEDATA structure filled in by the IFW1DWriteRenderTarget::DrawGlyphTemp method.</remarks>
		/// <returns>If the glyph is inserted, the index of the new glyph in the sheet is returned. This index can be used to get the glyph's coordinates from the sheet coord buffer. See IFW1GlyphSheet::GetCoordBuffer and IFW1GlyphSheet::GetGlyphCoords.<br/>
		/// If the method fails to insert the glyph, the returned value is 0xFFFFFFFF.</returns>
		/// <param name="pGlyphMetrics">A pointer to an FW1_GLYPHMETRICS structure, specifying the metrics of the glyph to be inserted.</param>
		/// <param name="pGlyphData">A pointer to image data.</param>
		/// <param name="RowPitch">The number of bytes in a row of image data.</param>
		/// <param name="PixelStride">The number of bytes between successive pixels.</param>
		virtual UINT STDMETHODCALLTYPE InsertGlyph(
			__in const FW1_GLYPHMETRICS *pGlyphMetrics,
			__in const void *pGlyphData,
			__in UINT RowPitch,
			__in UINT PixelStride
		) = 0;
		
		/// <summary>Close the sheet for additional glyphs.</summary>
		/// <remarks>After calling this method any subsequent attempts to insert new glyphs into the sheet will fail.
		/// Calling this method can save some memory as the RAM copy of the texture can be released after the next call to IFW1GlyphSheet::Flush.</remarks>
		/// <returns>No return value.</returns>
		virtual void STDMETHODCALLTYPE CloseSheet(
		) = 0;
		
		/// <summary>Flush any new glyphs to the internal D3D11 buffers.</summary>
		/// <remarks>When glyphs are inserted into the sheet only the CPU-memory resources are updated.
		/// In order for these to be available for use by the GPU, they must be flushed to the device using a device-context.</remarks>
		/// <returns>No return value.</returns>
		/// <param name="pContext">The context to use when updating device resources.</param>
		virtual void STDMETHODCALLTYPE Flush(
			__in ID3D11DeviceContext *pContext
		) = 0;
};

/// <summary>A glyph-atlas is a collection of glyph-sheets.</summary>
/// <remarks></remarks>
MIDL_INTERFACE("A31EB6A2-7458-4e24-82B3-945A95623B1F") IFW1GlyphAtlas : public IFW1Object {
	/// <summary>Get the ID3D11Device the atlas is created on.</summary>
	/// <remarks></remarks>
	/// <returns>Standard HRESULT error code.</returns>
	/// <param name="ppDevice">Address of a pointer to an ID3D11Device.</param>
	virtual HRESULT STDMETHODCALLTYPE GetDevice(
		__out ID3D11Device **ppDevice
	) = 0;
	
	/// <summary>Get the total number of glyphs in all the atlas' sheets.</summary>
	/// <remarks></remarks>
	/// <returns>The total number of glyphs in the atlas.</returns>
	virtual UINT STDMETHODCALLTYPE GetTotalGlyphCount(
	) = 0;
	
	/// <summary>Get the number of texture sheets in the atlas.</summary>
	/// <remarks></remarks>
	/// <returns>The number of sheets in the atlas.</returns>
	virtual UINT STDMETHODCALLTYPE GetSheetCount(
	) = 0;
	
	/// <summary>Get a pointer to an IFW1GlyphSheet in the atlas.</summary>
	/// <remarks></remarks>
	/// <returns>Standard HRESULT error code.</returns>
	/// <param name="SheetIndex">The index of the sheet to be obtained.</param>
	/// <param name="ppGlyphSheet">Address of a pointer to an IFW1GlyphSheet.</param>
	virtual HRESULT STDMETHODCALLTYPE GetSheet(
		__in UINT SheetIndex,
		__out IFW1GlyphSheet **ppGlyphSheet
	) = 0;
	
	/// <summary>Get a pointer to a sheet's glyph-coord buffer.</summary>
	/// <remarks></remarks>
	/// <returns>If <i>SheetIndex</i> is valid, returns a pointer to a coord buffer. See IFW1GlyphSheet::GetGlyphCoords.</returns>
	/// <param name="SheetIndex">The index of the sheet which coord buffer is to be obtained.</param>
	virtual const FW1_GLYPHCOORDS* STDMETHODCALLTYPE GetGlyphCoords(
		__in UINT SheetIndex
	) = 0;
	
	/// <summary>Bind a sheet's shader resources on a device context.</summary>
	/// <remarks></remarks>
	/// <returns>Standard HRESULT error code.</returns>
	/// <param name="pContext">The context to set the shader resources on.</param>
	/// <param name="SheetIndex">The index of the sheet to bind.</param>
	/// <param name="Flags">Flags that specify whether to set the geometry shader coord buffer. See IFW1GlyphSheet::BindSheet.</param>
	virtual HRESULT STDMETHODCALLTYPE BindSheet(
		__in ID3D11DeviceContext *pContext,
		__in UINT SheetIndex,
		__in UINT Flags
	) = 0;
	
	/// <summary>Insert a glyph into the atlas.</summary>
	/// <remarks>The parameters for this method can be obtained as part of the FW1_GLYPHIMAGEDATA structure filled in by the IFW1DWriteRenderTarget::DrawGlyphTemp method.</remarks>
	/// <returns>If the glyph is inserted, the ID of the new glyph in the atlas is returned.
	/// The ID is always <tt>((SheetIndex &lt;&lt; 16) | GlyphIndex)</tt>, where <i>SheetIndex</i> is the index of the sheet texture the glyph is place in, and <i>GlyphIndex</i> is the index of the glyph in that sheet.<br/>
	/// If the method fails to insert the glyph, the returned value is 0xFFFFFFFF.</returns>
	/// <param name="pGlyphMetrics">A pointer to an FW1_GLYPHMETRICS structure, specifying the metrics of the glyph to be inserted.</param>
	/// <param name="pGlyphData">A pointer to image data.</param>
	/// <param name="RowPitch">The number of bytes in a row of image data.</param>
	/// <param name="PixelStride">The number of bytes between successive pixels.</param>
	virtual UINT STDMETHODCALLTYPE InsertGlyph(
		__in const FW1_GLYPHMETRICS *pGlyphMetrics,
		__in const void *pGlyphData,
		__in UINT RowPitch,
		__in UINT PixelStride
	) = 0;
	
	/// <summary>Insert a sheet into the atlas.</summary>
	/// <remarks>This method is used internally whenever new glyphs no longer fits in existing sheets. The atlas will hold a reference to the sheet for the remainder of its lifetime.</remarks>
	/// <returns>On success, eturns the index of the sheet in the atlas after insertion.<br/>If the method fails, 0xFFFFFFFF is returned.</returns>
	/// <param name="pGlyphSheet">A pointer ot the glyph sheet to insert.</param>
	virtual UINT STDMETHODCALLTYPE InsertSheet(
		__in IFW1GlyphSheet *pGlyphSheet
	) = 0;
	
	/// <summary>Flush all new or internally updated sheets.</summary>
	/// <remarks>See IFW1GlyphSheet::Flush.</remarks>
	/// <returns>No return value.</returns>
	/// <param name="pContext">The context to use when updating device resources.</param>
	virtual void STDMETHODCALLTYPE Flush(
		__in ID3D11DeviceContext *pContext
	) = 0;
};

/// <summary>Collection of glyph-maps, mapping font/size/glyph information to an ID in a glyph atlas.</summary>
/// <remarks>Whenever a glyph or glyphmap is queried from the glyph-provider, it will be dynamically inserted if it does not already exist.</remarks>
MIDL_INTERFACE("F8360043-329D-4EC9-B0F8-ACB00FA77420") IFW1GlyphProvider : public IFW1Object {
	/// <summary>Get the IFW1GlyphAtlas this glyph-provider references.</summary>
	/// <remarks></remarks>
	/// <returns>Standard HRESULT error code.</returns>
	/// <param name="ppGlyphAtlas">Address of a pointer to an IFW1GlyphAtlas.</param>
	virtual HRESULT STDMETHODCALLTYPE GetGlyphAtlas(
		__out IFW1GlyphAtlas **ppGlyphAtlas
	) = 0;
	
	/// <summary>Get the DirectWrite factory a glyph-provider references.</summary>
	/// <remarks>The DirectWrite factory is used internally to create render-targets needed to draw glyph-images that are put in the glyph-atlas.</remarks>
	/// <returns>Standard HRESULT error code.</returns>
	/// <param name="ppDWriteFactory">Address of a pointer to an IDWriteFactory.</param>
	virtual HRESULT STDMETHODCALLTYPE GetDWriteFactory(
		__out IDWriteFactory **ppDWriteFactory
	) = 0;
	
	/// <summary>Get the DirectWrite font-collection referenced by a glyph-provider.</summary>
	/// <remarks>The DirectWrite font collection is used internally only to match font-faces to unique names.
	/// Since different IDWriteFontFace objects can reference the same font, a reliable method of identifying a font is required.
	/// This is the only function of the font-collection in the scope of the glyph-provider.
	/// A font-face not from the same collection can still be used when requesting glyphs, in which case it will only be identified by its pointer value.</remarks>
	/// <returns>Standard HRESULT error code.</returns>
	/// <param name="ppFontCollection">Address of a pointer to an IDWriteFontCollection.</param>
	virtual HRESULT STDMETHODCALLTYPE GetDWriteFontCollection(
		__out IDWriteFontCollection **ppFontCollection
	) = 0;
	
	/// <summary>Get a pointer identifying a glyph-map matching the specified font.</summary>
	/// <remarks>If the FW1_NONEWGLYPHS flag is not specified, new glyph-maps are created on demand.
	/// The glyph-map returned from this method is not meaningful outside of the glyph provider internals, but is needed for subsequent calls to IFW1GlyphProvider::GetAtlasIdFromGlyphIndex.</remarks>
	/// <returns>A constant pointer identifying a glyph-map.
	/// This pointer should only be used in subsequent calls to IFW1GlyphProvider::GetAtlasIdFromGlyphIndex, and remains valid for the lifetime of the IFW1GlyphProvider.<br/>
	/// A NULL pointer may be returned, if using the FW1_NONEWGLYPHS flag and a matching glyph-map does not exist.</returns>
	/// <param name="pFontFace">A DirectWrite font face.</param>
	/// <param name="FontSize">The size of the font.</param>
	/// <param name="FontFlags">Can include zero or more of the following values, ORd together. Any additional values are ignored.<br/>
	/// FW1_ALIASED - No anti-aliasing is used when drawing the glyphs.<br/>
	/// FW1_NONEWGLYPHS - No new glyph-maps are created.</param>
	virtual const void* STDMETHODCALLTYPE GetGlyphMapFromFont(
		__in IDWriteFontFace *pFontFace,
		__in FLOAT FontSize,
		__in UINT FontFlags
	) = 0;
	
	/// <summary>Get the ID of the specified glyph in the glyph-atlas.</summary>
	/// <remarks>If FW1_NONEWGLYPHS is not specified, any glyph not currently in the atlas will be inserted before the method returns.</remarks>
	/// <returns>The ID of the specified glyph in the glyph-atlas.
	/// The ID is always <tt>((SheetIndex &lt;&lt; 16) | GlyphIndex)</tt>, where <i>SheetIndex</i> is the index of the sheet texture the glyph is placed in, and <i>GlyphIndex</i> is the index of the glyph in that sheet.<br/>
	/// If the specified glyph does not exist and can not be inserted on demand, the ID of a fallback glyph is returned.</returns>
	/// <param name="pGlyphMap">A pointer identifying a glyph-map, previously obtained using IFW1GlyphProvider::GetGlyphMapFromFont.
	/// If this parameter is NULL, the ID of the last-resort fallback glyph is returned, which will be zero.</param>
	/// <param name="GlyphIndex">The index of the glyph in the DirectWrite font face.
	/// Glyph indices can be obtained from DirectWrite using IDWriteFontFace::GetGlyphIndices.</param>
	/// <param name="pFontFace">The DirectWrite font face that contains the glyph referenced by GlyphIndex.</param>
	/// <param name="FontFlags">Can include zero or more of the following values, ORd together. Any additional values are ignored.<br/>
	/// FW1_NONEWGLYPHS - No new glyphs are inserted.</param>
	virtual UINT STDMETHODCALLTYPE GetAtlasIdFromGlyphIndex(
		__in const void *pGlyphMap,
		__in UINT16 GlyphIndex,
		__in IDWriteFontFace *pFontFace,
		__in UINT FontFlags
	) = 0;
};

/// <summary>Container for a DirectWrite render-target, used to draw glyph images that are to be inserted in a glyph atlas.</summary>
/// <remarks></remarks>
MIDL_INTERFACE("A1EB4141-9A66-4097-A5B0-6FC84F8B162C") IFW1DWriteRenderTarget : public IFW1Object {
	/// <summary>Draw a glyph-image.</summary>
	/// <remarks>The data returned in the FW1_GLYPHIMAGEDATA should only be read, and is valid until the next call to a method in the IFW1DWriteRenderTarget.<br/>
	/// This method is not thread-safe.</remarks>
	/// <returns>Standard HRESULT error code.</returns>
	/// <param name="pFontFace">The DirectWrite font face containing the glyph.</param>
	/// <param name="GlyphIndex">The index of the glyph in the font face.</param>
	/// <param name="FontSize">The size of the font.</param>
	/// <param name="RenderingMode">The DirectWrite rendering mode. See DirectWrite documentation.</param>
	/// <param name="MeasuringMode">The DirectWrite measuring mode. See DirectWrite documentation..</param>
	/// <param name="pOutData">A pointer to an FW1_GLYPHIMAGEDATA structure that will be filled in with the glyph image data on success.</param>
	virtual HRESULT STDMETHODCALLTYPE DrawGlyphTemp(
		__in IDWriteFontFace *pFontFace,
		__in UINT16 GlyphIndex,
		__in FLOAT FontSize,
		__in DWRITE_RENDERING_MODE RenderingMode,
		__in DWRITE_MEASURING_MODE MeasuringMode,
		__out FW1_GLYPHIMAGEDATA *pOutData
	) = 0;
};

/// <summary>An RGBA color.</summary>
/// <remarks>An IFW1ColorRGBA object can be set as the drawing effect for a range in an IDWriteTextLayout, to override the default color of the text.</remarks>
MIDL_INTERFACE("A0EA03A0-441D-49BE-9D2C-4AE27BB7A327") IFW1ColorRGBA : public IFW1Object {
	/// <summary>Set the color.</summary>
	/// <remarks></remarks>
	/// <returns>No return value.</returns>
	/// <param name="Color">The color to set, as 0xAaBbGgRr.</param>
	virtual void STDMETHODCALLTYPE SetColor(
		__in UINT32 Color
	) = 0;
	
	/// <summary>Set the color.</summary>
	/// <remarks></remarks>
	/// <returns>No return value.</returns>
	/// <param name="Red">The red component, in [0, 1].</param>
	/// <param name="Green">The green component, in [0, 1].</param>
	/// <param name="Blue">The blue component, in [0, 1].</param>
	/// <param name="Alpha">The alpha component, in [0, 1].</param>
	virtual void STDMETHODCALLTYPE SetColor(
		__in FLOAT Red,
		__in FLOAT Green,
		__in FLOAT Blue,
		__in FLOAT Alpha
	) = 0;
	
	/// <summary>Set the color.</summary>
	/// <remarks></remarks>
	/// <returns>No return value.</returns>
	/// <param name="pColor">Pointer to an array of four floats in [0, 1], specifying the red, green, blue and alpha components at indices 0 to 3.</param>
	virtual void STDMETHODCALLTYPE SetColor(
		__in const FLOAT *pColor
	) = 0;
	
	/// <summary>Set the color.</summary>
	/// <remarks></remarks>
	/// <returns>No return value.</returns>
	/// <param name="pColor">Pointer to an array of four bytes in [0, 255], specifying the red, green, blue and alpha components at indices 0 to 3.</param>
	virtual void STDMETHODCALLTYPE SetColor(
		__in const BYTE *pColor
	) = 0;
	
	/// <summary>Get the color.</summary>
	/// <remarks></remarks>
	/// <returns>Returns the color, as 0xAaBbGgRr.</returns>
	virtual UINT32 STDMETHODCALLTYPE GetColor32(
	) = 0;
};

/// <summary>A dynamic list of vertices. Note that this object is a simple array without synchronization and not safe to use simultaneously on more than one thread.</summary>
/// <remarks>When rendering a string, a vertex is inserted into an IFW1TextGeometry object for each glyph.
/// The vertices in an IFW1TextGeometry can be drawn by the IFW1FontWrapper::DrawGeometry method.<br/>
/// A pointer to the actual vertices can be obtained with the IFW1TextGeometry::GetGlyphVerticesTemp method.</remarks>
MIDL_INTERFACE("51E05736-6AFF-44A8-9745-77605C99E8F2") IFW1TextGeometry : public IFW1Object {
	/// <summary>Clear any vertices currently contained in the geometry object.</summary>
	/// <remarks>This method is not thread-safe.</remarks>
	/// <returns>No return value.</returns>
	virtual void STDMETHODCALLTYPE Clear(
	) = 0;
	
	/// <summary>Adds a vertex to the geometry.</summary>
	/// <remarks>The GlyphIndex member of the FW1_GLYPHVERTEX specified when inserting a vertex should be the atlas ID of the desired glyph, as <tt>((SheetIndex &lt;&lt; 16) | GlyphIndex)</tt>.
	/// See IFW1GlyphAtlas::InsertGlyph.<br/>
	/// This method is not thread-safe.</remarks>
	/// <returns>No return value.</returns>
	/// <param name="pVertex">Pointer to an FW1_GLYPHVERTEX structure describing the vertex.</param>
	virtual void STDMETHODCALLTYPE AddGlyphVertex(
		__in const FW1_GLYPHVERTEX *pVertex
	) = 0;
	
	/// <summary>Get the vertices in the geometry, sorted by glyph sheet.</summary>
	/// <remarks>When glyphs are inserted into the geometry they contain their glyph atlas ID.
	/// The glyphs are internally sorted by glyph sheet, and glyphs returned by GetGlyphVerticesTemp contain the index of the glyph in its containing sheet, and not the atlas ID.<br/>
	/// This method is not thread-safe.</remarks>
	/// <returns>An FW1_VERTEXDATA structure containing the glyph vertices.
	/// The pointers in this structure are owned by the geometry object and should not be modified.
	/// They are valid until the next call to a method in the IFW1TextGeometry.</returns>
	virtual FW1_VERTEXDATA STDMETHODCALLTYPE GetGlyphVerticesTemp(
	) = 0;
};

/// <summary>A text-renderer converts DirectWrite text layouts into glyph-vertices.</summary>
/// <remarks></remarks>
MIDL_INTERFACE("51E05736-6AFF-44A8-9745-77605C99E8F2") IFW1TextRenderer : public IFW1Object {
	/// <summary>Get the IFW1GlyphProvider used by a text-renderer.</summary>
	/// <remarks>The glyph provider is used internally to get the atlas IDs for any glyphs needed when drawing a text layout.</remarks>
	/// <returns>Standard HRESULT error code.</returns>
	/// <param name="ppGlyphProvider">Address of a pointer to an IFW1GlyphProvider.</param>
	virtual HRESULT STDMETHODCALLTYPE GetGlyphProvider(
		__out IFW1GlyphProvider **ppGlyphProvider
	) = 0;
	
	/// <summary>Convert a text layout to vertices.</summary>
	/// <remarks>This method internally calls the IDWriteTextLayout::Draw method, and handles callbacks to convert the formatted text into vertices, which will be stored in the passed IFW1TextGeometry object.
	/// This method is not thread-safe.</remarks>
	/// <returns>Standard HRESULT error code.</returns>
	/// <param name="pTextLayout">A DirectWrite text layout. See the DirectWrite documentation.</param>
	/// <param name="OriginX">The X origin of the text.</param>
	/// <param name="OriginY">The Y origin of the text.</param>
	/// <param name="Color">The default text color, as 0xAaGgBbRr.</param>
	/// <param name="Flags">Can include zero or more of the following values, ORd together. Any additional values are ignored.<br/>
	/// FW1_ALIASED - No anti-aliasing is used when drawing the glyphs.<br/>
	/// FW1_NONEWGLYPHS - No new glyphs are inserted into the atlas. Not previously cached glyphs are replaced with a fallback glyph (usually an empty box).<br/>
	/// FW1_CACHEONLY - All glyphs are queried from the glyph-provider and cached in the glyph-atlas, but no geometry is produced.<br/>
	/// FW1_ANALYZEONLY - The text-layout is analyzed and glyph-maps are prepared, but the glyphs in the string are not cached and no geometry is produced.<br/>
	/// </param>
	/// <param name="pTextGeometry">An IFW1TextGeometry object that the output vertices will be appended to.</param>
	virtual HRESULT STDMETHODCALLTYPE DrawTextLayout(
		__in IDWriteTextLayout *pTextLayout,
		__in FLOAT OriginX,
		__in FLOAT OriginY,
		__in UINT32 Color,
		__in UINT Flags,
		__in IFW1TextGeometry *pTextGeometry
	) = 0;
};

/// <summary>This interface contains all render states and shaders needed to draw glyphs.</summary>
/// <remarks></remarks>
MIDL_INTERFACE("906928B6-79D8-4b42-8CE4-DC7D7046F206") IFW1GlyphRenderStates : public IFW1Object {
	/// <summary>Get the ID3D11Device that all render states are created on.</summary>
	/// <remarks></remarks>
	/// <returns>Standard HRESULT error code.</returns>
	/// <param name="ppDevice">Address of a pointer to an ID3D11Device.</param>
	virtual HRESULT STDMETHODCALLTYPE GetDevice(
		__out ID3D11Device **ppDevice
	) = 0;
	
	/// <summary>Set the internal states on a context.</summary>
	/// <remarks></remarks>
	/// <returns>No return value.</returns>
	/// <param name="pContext">The context to set the states on.</param>
	/// <param name="Flags">Can include zero or more of the following values, ORd together. Any additional values are ignored.<br/>
	/// FW1_NOGEOMETRYSHADER - States are set up to draw indexed quads instead of constructing quads in the geometry shader.<br/>
	/// FW1_CLIPRECT - Shaders will be set up to clip any drawn glyphs to the clip-rect set in IFW1GlyphRenderStates::UpdateShaderConstants.
	/// </param>
	virtual void STDMETHODCALLTYPE SetStates(
		__in ID3D11DeviceContext *pContext,
		__in UINT Flags
	) = 0;
	
	/// <summary>Update the internal constant buffer.</summary>
	/// <remarks></remarks>
	/// <returns>No return value.</returns>
	/// <param name="pContext">The context to use to update the constant buffer.</param>
	/// <param name="pClipRect">A pointer to a rectangle to clip drawn glyphs to.</param>
	/// <param name="pTransformMatrix">An array of 16 floats, representing a matrix which all glyph vertices will be multiplied with, in the geometry or vertex shader.</param>
	virtual void STDMETHODCALLTYPE UpdateShaderConstants(
		__in ID3D11DeviceContext *pContext,
		__in const FW1_RECTF *pClipRect,
		__in const FLOAT *pTransformMatrix
	) = 0;
	
	/// <summary>Returns whether a geometry shader is available.</summary>
	/// <remarks>When an IFW1GlyphRenderStates object is created, it may attempt to create a geometry shader, depending on the parameters passed to IFW1Factory::CreateRenderStates.
	/// If a geometry shader is not created, either because of the specified parameters or because the device feature level does not support geometry shaders, this method will return FALSE.</remarks>
	/// <returns>Returns TRUE if a geometry shader is available, and otherwise returns FALSE.</returns>
	virtual BOOL STDMETHODCALLTYPE HasGeometryShader(
	) = 0;
};

/// <summary>A container for a dynamic vertex and index buffer, used to draw glyph vertices.</summary>
/// <remarks></remarks>
MIDL_INTERFACE("E6CD7A32-5B59-463c-9B1B-D44074FF655B") IFW1GlyphVertexDrawer : public IFW1Object {
	/// <summary>Get the ID3D11Device that the buffers are created on.</summary>
	/// <remarks></remarks>
	/// <returns>Standard HRESULT error code.</returns>
	/// <param name="ppDevice">Address of a pointer to an ID3D11Device.</param>
	virtual HRESULT STDMETHODCALLTYPE GetDevice(
		__out ID3D11Device **ppDevice
	) = 0;
	
	/// <summary>Upload the specified vertices to the device buffers and draw them.</summary>
	/// <remarks></remarks>
	/// <returns>Returns the index of the sheet in the atlas that was last bound to the device context during the operation.</returns>
	/// <param name="pContext">The context to use to draw.</param>
	/// <param name="pGlyphAtlas">The glyph atlas containg the glyphs referenced by the vertices.</param>
	/// <param name="pVertexData">Pointer to an FW1_VERTEXDATA structure, containing vertices to be drawn, sorted by glyph sheet.
	/// These are easiest obtained from an IFW1TextGeometry object.</param>
	/// <param name="Flags">Can include zero or more of the following values, ORd together. Any additional values are ignored.<br/>
	/// FW1_NOGEOMETRYSHADER - Vertices are converted to quads on the fly on the CPU, instead of being sent directly to the device for the geometry shader.<br/>
	/// FW1_BUFFERSPREPARED - The internal buffers are assumed to already be set on the device context from a previous call. (Avoids redundant state changes when drawing multiple times).
	/// </param>
	/// <param name="PreboundSheet">If a sheet in the atlas is known to already be correctly set on the device context, specify its index in the atlas with this parameter, to avoid redundant state changes.
	/// If no sheet is known to already be set, specify 0xFFFFFFFF, or another value greater than the number of sheets in the atlas.
	/// </param>
	virtual UINT STDMETHODCALLTYPE DrawVertices(
		__in ID3D11DeviceContext *pContext,
		__in IFW1GlyphAtlas *pGlyphAtlas,
		__in const FW1_VERTEXDATA *pVertexData,
		__in UINT Flags,
		__in UINT PreboundSheet
	) = 0;
};

/// <summary>The IFW1FontWrapper interface is the main interface used to draw text.
/// It holds references to all objects needed to format and convert text to vertices, as well as the D3D11 states and buffers needed to draw them.</summary>
/// <remarks>Create a font-wrapper using IFW1Factory::CreateFontWrapper</remarks>
MIDL_INTERFACE("83347A5C-B0B1-460e-A35C-427E8B85F9F4") IFW1FontWrapper : public IFW1Object {
	/// <summary>Get the ID3D11Device that is used by the font-wrapper.</summary>
	/// <remarks></remarks>
	/// <returns>Standard HRESULT error code.</returns>
	/// <param name="ppDevice">Address of a pointer to an ID3D11Device.</param>
	virtual HRESULT STDMETHODCALLTYPE GetDevice(
		__out ID3D11Device **ppDevice
	) = 0;
	
	/// <summary>Get the DirectWrite factory used by the font-wrapper.</summary>
	/// <remarks>The DirectWrite factory is used internally to create text-layouts when drawing strings using any of the DrawString methods.</remarks>
	/// <returns>Standard HRESULT error code.</returns>
	/// <param name="ppDWriteFactory">Address of a pointer to an IDWriteFactory.</param>
	virtual HRESULT STDMETHODCALLTYPE GetDWriteFactory(
		__out IDWriteFactory **ppDWriteFactory
	) = 0;
	
	/// <summary>Get the IFW1GlyphAtlas used to cache glyphs.</summary>
	/// <remarks></remarks>
	/// <returns>Standard HRESULT error code.</returns>
	/// <param name="ppGlyphAtlas">Address of a pointer to an IFW1GlyphAtlas.</param>
	virtual HRESULT STDMETHODCALLTYPE GetGlyphAtlas(
		__out IFW1GlyphAtlas **ppGlyphAtlas
	) = 0;
	
	/// <summary>Get the IFW1GlyphProvider used to map glyphs to atlas IDs.</summary>
	/// <remarks></remarks>
	/// <returns>Standard HRESULT error code.</returns>
	/// <param name="ppGlyphProvider">Address of a pointer to an IFW1GlyphProvider.</param>
	virtual HRESULT STDMETHODCALLTYPE GetGlyphProvider(
		__out IFW1GlyphProvider **ppGlyphProvider
	) = 0;
	
	/// <summary>Get the IFW1GlyphRenderStates containing the render states needed to draw glyphs.</summary>
	/// <remarks></remarks>
	/// <returns>Standard HRESULT error code.</returns>
	/// <param name="ppRenderStates">Address of a pointer to an IFW1GlyphRenderStates.</param>
	virtual HRESULT STDMETHODCALLTYPE GetRenderStates(
		__out IFW1GlyphRenderStates **ppRenderStates
	) = 0;
	
	/// <summary>Get the IFW1GlyphVertexDrawer used to draw glyph vertices.</summary>
	/// <remarks></remarks>
	/// <returns>Standard HRESULT error code.</returns>
	/// <param name="ppVertexDrawer">Address of a pointer to an IFW1GlyphVertexDrawer.</param>
	virtual HRESULT STDMETHODCALLTYPE GetVertexDrawer(
		__out IFW1GlyphVertexDrawer **ppVertexDrawer
	) = 0;
	
	/// <summary>Draw a DirectWrite text layout.</summary>
	/// <remarks>Consult the DirectWrite documentation for details on how to construct a text-layout.<br/>
	/// The pContext parameter can be NULL only if the FW1_NOFLUSH and either the FW1_ANALYZEONLY or the FW1_CACHEONLY flags are specified.</remarks>
	/// <returns>No return value.</returns>
	/// <param name="pContext">The device context to draw on.</param>
	/// <param name="pTextLayout">The text layout to draw.</param>
	/// <param name="OriginX">The X origin of the text in the layout.</param>
	/// <param name="OriginY">The Y origin of the text in the layout.</param>
	/// <param name="Color">The default color of the text, as 0xAaBbGgRr.</param>
	/// <param name="Flags">See FW1_TEXT_FLAG. The alignment and word-wrapping flags have no meaning when drawing a preconstructed text layout.</param>
	virtual void STDMETHODCALLTYPE DrawTextLayout(
		__in ID3D11DeviceContext *pContext,
		__in IDWriteTextLayout *pTextLayout,
		__in FLOAT OriginX,
		__in FLOAT OriginY,
		__in UINT32 Color,
		__in UINT Flags
	) = 0;
	
	/// <summary>Draw a DirectWrite text layout.</summary>
	/// <remarks>Consult the DirectWrite documentation for details on how to construct a text-layout.<br/>
	/// The pContext parameter can be NULL only if the FW1_NOFLUSH and either the FW1_ANALYZEONLY or the FW1_CACHEONLY flags are specified.</remarks>
	/// <returns>No return value.</returns>
	/// <param name="pContext">The device context to draw on.</param>
	/// <param name="pTextLayout">The text layout to draw.</param>
	/// <param name="OriginX">The X origin of the text in the layout.</param>
	/// <param name="OriginY">The Y origin of the text in the layout.</param>
	/// <param name="Color">The default color of the text, as 0xAaBbGgRr.</param>
	/// <param name="pClipRect">A pointer to a rectangle to clip the text to if also using the FW1_CLIPRECT flag, or NULL to not clip.</param>
	/// <param name="pTransformMatrix">An array of 16 floats, representing a matrix which the text will be transformed by.</param>
	/// <param name="Flags">See FW1_TEXT_FLAG. The alignment and word-wrapping flags have no meaning when drawing a preconstructed text layout.</param>
	virtual void STDMETHODCALLTYPE DrawTextLayout(
		__in ID3D11DeviceContext *pContext,
		__in IDWriteTextLayout *pTextLayout,
		__in FLOAT OriginX,
		__in FLOAT OriginY,
		__in UINT32 Color,
		__in const FW1_RECTF *pClipRect,
		__in const FLOAT *pTransformMatrix,
		__in UINT Flags
	) = 0;
	
	/// <summary>Draw a string.</summary>
	/// <remarks>The pContext parameter can be NULL only if the FW1_NOFLUSH and either the FW1_ANALYZEONLY or the FW1_CACHEONLY flags are specified.</remarks>
	/// <returns>No return value.</returns>
	/// <param name="pContext">The device context to draw on.</param>
	/// <param name="pszString">The NULL-terminated string to draw.</param>
	/// <param name="FontSize">The size of the font.</param>
	/// <param name="X">The X origin of the text.</param>
	/// <param name="Y">The Y origin of the text .</param>
	/// <param name="Color">The color of the text, as 0xAaBbGgRr.</param>
	/// <param name="Flags">See the FW1_TEXT_FLAG enumeration.</param>
	virtual void STDMETHODCALLTYPE DrawString(
		__in ID3D11DeviceContext *pContext,
		__in const WCHAR *pszString,
		__in FLOAT FontSize,
		__in FLOAT X,
		__in FLOAT Y,
		__in UINT32 Color,
		__in UINT Flags
	) = 0;
	
	/// <summary>Draw a string.</summary>
	/// <remarks>The pContext parameter can be NULL only if the FW1_NOFLUSH and either the FW1_ANALYZEONLY or the FW1_CACHEONLY flags are specified.</remarks>
	/// <returns>No return value.</returns>
	/// <param name="pContext">The device context to draw on.</param>
	/// <param name="pszString">The NULL-terminated string to draw.</param>
	/// <param name="pszFontFamily">The font family to use, such as Arial or Courier New.</param>
	/// <param name="FontSize">The size of the font.</param>
	/// <param name="X">The X origin of the text.</param>
	/// <param name="Y">The Y origin of the text .</param>
	/// <param name="Color">The color of the text, as 0xAaBbGgRr.</param>
	/// <param name="Flags">See the FW1_TEXT_FLAG enumeration.</param>
	virtual void STDMETHODCALLTYPE DrawString(
		__in ID3D11DeviceContext *pContext,
		__in const WCHAR *pszString,
		__in const WCHAR *pszFontFamily,
		__in FLOAT FontSize,
		__in FLOAT X,
		__in FLOAT Y,
		__in UINT32 Color,
		__in UINT Flags
	) = 0;
	
	/// <summary>Draw a string.</summary>
	/// <remarks>The pContext parameter can be NULL only if the FW1_NOFLUSH and either the FW1_ANALYZEONLY or the FW1_CACHEONLY flags are specified.</remarks>
	/// <returns>No return value.</returns>
	/// <param name="pContext">The device context to draw on.</param>
	/// <param name="pszString">The NULL-terminated string to draw.</param>
	/// <param name="pszFontFamily">The font family to use, such as Arial or Courier New.</param>
	/// <param name="FontSize">The size of the font.</param>
	/// <param name="pLayoutRect">A pointer to a rectangle to format the text in.</param>
	/// <param name="Color">The color of the text, as 0xAaBbGgRr.</param>
	/// <param name="pClipRect">A pointer to a rectangle to clip the text to if also using the FW1_CLIPRECT flag, or NULL to not clip.</param>
	/// <param name="pTransformMatrix">An array of 16 floats, representing a matrix which the text will be transformed by, or NULL to draw in screen-space.</param>
	/// <param name="Flags">See the FW1_TEXT_FLAG enumeration.</param>
	virtual void STDMETHODCALLTYPE DrawString(
		__in ID3D11DeviceContext *pContext,
		__in const WCHAR *pszString,
		__in const WCHAR *pszFontFamily,
		__in FLOAT FontSize,
		__in const FW1_RECTF *pLayoutRect,
		__in UINT32 Color,
		__in const FW1_RECTF *pClipRect,
		__in const FLOAT *pTransformMatrix,
		__in UINT Flags
	) = 0;
	
	/// <summary>Measure a string.</summary>
	/// <remarks>This function uses the IDWriteTextLayout::GetOverhangMetrics to obtain the size of the string.</remarks>
	/// <returns>The smallest rectangle that completely contains the string if drawn with DrawString and the same parameters as used with MeasureString.</returns>
	/// <param name="pszString">The NULL-terminated string to measure.</param>
	/// <param name="pszFontFamily">The font family to use, such as Arial or Courier New.</param>
	/// <param name="FontSize">The size of the font.</param>
	/// <param name="pLayoutRect">A pointer to a rectangle to format the text in.</param>
	/// <param name="Flags">See the FW1_TEXT_FLAG enumeration.</param>
	virtual FW1_RECTF STDMETHODCALLTYPE MeasureString(
		__in const WCHAR *pszString,
		__in const WCHAR *pszFontFamily,
		__in FLOAT FontSize,
		__in const FW1_RECTF *pLayoutRect,
		__in UINT Flags
	) = 0;
	
	/// <summary>Analyze a string and generate geometry to draw it.</summary>
	/// <remarks>pTextGeometry can be NULL if the FW1_ANALYZEONLY or FW1_CACHEONLY flags are specified, as no actual geometry will be generated.
	/// pContext can be NULL if the FW1_NOFLUSH flag is used, as any new glyphs will not be flushed to the device buffers.</remarks>
	/// <returns>No return value.</returns>
	/// <param name="pContext">A device context to use to update device buffers when new glyphs are added to the glyph-atlas.</param>
	/// <param name="pszString">The NULL-terminated string to create geometry from.</param>
	/// <param name="pszFontFamily">The font family to use, such as Arial or Courier New.</param>
	/// <param name="FontSize">The size of the font.</param>
	/// <param name="pLayoutRect">A pointer to a rectangle to format the text in.</param>
	/// <param name="Color">The color of the text, as 0xAaBbGgRr.</param>
	/// <param name="Flags">See the FW1_TEXT_FLAG enumeration.</param>
	/// <param name="pTextGeometry">An IFW1TextGeometry object that the output vertices will be appended to.</param>
	virtual void STDMETHODCALLTYPE AnalyzeString(
		__in ID3D11DeviceContext *pContext,
		__in const WCHAR *pszString,
		__in const WCHAR *pszFontFamily,
		__in FLOAT FontSize,
		__in const FW1_RECTF *pLayoutRect,
		__in UINT32 Color,
		__in UINT Flags,
		__in IFW1TextGeometry *pTextGeometry
	) = 0;
	
	/// <summary>Analyze a text layout and generate geometry to draw it.</summary>
	/// <remarks>Consult the DirectWrite documentation for details on how to construct a text-layout.
	/// pTextGeometry can be NULL if the FW1_ANALYZEONLY or FW1_CACHEONLY flags are specified, as no actual geometry will be generated.
	/// pContext can be NULL if the FW1_NOFLUSH flag is used, as any new glyphs will not be flushed to the device buffers.</remarks>
	/// <returns>No return value.</returns>
	/// <param name="pContext">A device context to use to update device buffers when new glyphs are added to the glyph-atlas.</param>
	/// <param name="pTextLayout">The DirectWrite text layout to create geometry from.</param>
	/// <param name="OriginX">The X origin of the text in the layout.</param>
	/// <param name="OriginY">The Y origin of the text in the layout.</param>
	/// <param name="Color">The default color of the text, as 0xAaBbGgRr.</param>
	/// <param name="Flags">See FW1_TEXT_FLAG. The alignment and word-wrapping flags have no meaning when using a preconstructed text layout.</param>
	/// <param name="pTextGeometry">An IFW1TextGeometry object that the output vertices will be appended to.</param>
	virtual void STDMETHODCALLTYPE AnalyzeTextLayout(
		__in ID3D11DeviceContext *pContext,
		__in IDWriteTextLayout *pTextLayout,
		__in FLOAT OriginX,
		__in FLOAT OriginY,
		__in UINT32 Color,
		__in UINT Flags,
		__in IFW1TextGeometry *pTextGeometry
	) = 0;
	
	/// <summary>Draw geometry.</summary>
	/// <remarks></remarks>
	/// <returns>No return value.</returns>
	/// <param name="pContext">The device context to draw on.</param>
	/// <param name="pGeometry">The geometry to draw.</param>
	/// <param name="pClipRect">A pointer to a rectangle to clip the text to if also using the FW1_CLIPRECT flag, or NULL to not clip. This rect is in text-space, and clipping is performed prior to any transformation.</param>
	/// <param name="pTransformMatrix">An array of 16 floats, representing a matrix which the text will be transformed by, or NULL to draw in screen-space.</param>
	/// <param name="Flags">See the FW1_TEXT_FLAG enumeration.</param>
	virtual void STDMETHODCALLTYPE DrawGeometry(
		__in ID3D11DeviceContext *pContext,
		__in IFW1TextGeometry *pGeometry,
		__in const FW1_RECTF *pClipRect,
		__in const FLOAT *pTransformMatrix,
		__in UINT Flags
	) = 0;
	
	/// <summary>Flush any new glyphs to GPU resources.</summary>
	/// <remarks>This method calls IFW1GlyphAtlas::Flush to flush any newly cached glyphs.
	/// This method is only needed if drawing text using the FW1_NOFLUSH flag and delaying flushing data to the device, as otherwise it is implicitly called whenever a string is drawn. See IFW1GlyphAtlas::Flush.</remarks>
	/// <returns>No return value.</returns>
	/// <param name="pContext">The device context to use to update device resources.</param>
	virtual void STDMETHODCALLTYPE Flush(
		__in ID3D11DeviceContext *pContext
	) = 0;
};

/// <summary>
/// Used to create all subsequent FW1 objects.
/// An IFW1Factory can be created using FW1CreateFactory.
/// </summary>
/// <remarks>
/// Any and all FW1 objects are always created through a factory. All FW1 objects inherit from IFW1Object, and holds a reference to the factory that created them.
/// If an object in turn creates new objects, it uses the same factory that created the object itself.
/// For example, a glyph-atlas will ask the factory that created it to create new glyph-sheets as glyphs are added to the atlas.
/// </remarks>
MIDL_INTERFACE("8004DB2B-B5F9-4420-A6A2-E17E15E4C336") IFW1Factory : public IUnknown {
	public:
		/// <summary>Create an IFW1FontWrapper object with default settings.</summary>
		/// <returns>Standard HRESULT error code.</returns>
		/// <remarks></remarks>
		/// <param name="pDevice">The ID3D11Device the font-wrapper will be used with.</param>
		/// <param name="pszFontFamily">The default font-family to use when drawing strings.
		/// Valid values include for example L"Arial" and L"Courier New", provided that the fonts are installed on the system.
		/// Font-fallback will automatically choose a different font if the specified one is not available.</param>
		/// <param name="ppFontWrapper">Address of a pointer to a font-wrapper (See IFW1FontWrapper).</param>
		virtual HRESULT STDMETHODCALLTYPE CreateFontWrapper(
			__in ID3D11Device *pDevice,
			__in LPCWSTR pszFontFamily,
			__out IFW1FontWrapper **ppFontWrapper
		) = 0;
		
		/// <summary>Create an IFW1FontWrapper object.</summary>
		/// <remarks></remarks>
		/// <returns>Standard HRESULT error code.</returns>
		/// <param name="pDevice">The ID3D11Device that the font-wrapper will be used with.</param>
		/// <param name="pCreateParams">Pointer to an FW1_FONTWRAPPERCREATEPARAMS structure that describes the settings for the new font-wrapper.</param>
		/// <param name="ppFontWrapper">Address of a pointer to a font-wrapper (See IFW1FontWrapper).</param>
		virtual HRESULT STDMETHODCALLTYPE CreateFontWrapper(
			__in ID3D11Device *pDevice,
			__in IDWriteFactory *pDWriteFactory,
			__in const FW1_FONTWRAPPERCREATEPARAMS *pCreateParams,
			__out IFW1FontWrapper **ppFontWrapper
		) = 0;
		
		/// <summary>Create an IFW1FontWrapper object.</summary>
		/// <remarks></remarks>
		/// <returns>Standard HRESULT error code.</returns>
		/// <param name="pDevice">The ID3D11Device that the font-wrapper will be used with.</param>
		/// <param name="pGlyphAtlas">An IFW1GlyphAtlas that glyph-images will be stored in.</param>
		/// <param name="pGlyphProvider">An IFW1GlyphProvider that handles fonts and glyphmaps.</param>
		/// <param name="pGlyphVertexDrawer">An IFW1GlyphVertexDrawer that handles drawing glyph-vertices.</param>
		/// <param name="pGlyphRenderStates">An IFW1GlyphRenderStates that handles all needed context states when drawing glyphs.</param>
		/// <param name="pDWriteFactory">An IDWriteFactory that is used to create render-targets to draw glyphs with.</param>
		/// <param name="pDefaultFontParams">Pointer to an FW1_DWRITEFONTPARAMS that describes the default font.
		/// Can be NULL if only drawing preconstructed DirectWrite text layouts.</param>
		/// <param name="ppFontWrapper">Address of a pointer to a font-wrapper (See IFW1FontWrapper).</param>
		virtual HRESULT STDMETHODCALLTYPE CreateFontWrapper(
			__in ID3D11Device *pDevice,
			__in IFW1GlyphAtlas *pGlyphAtlas,
			__in IFW1GlyphProvider *pGlyphProvider,
			__in IFW1GlyphVertexDrawer *pGlyphVertexDrawer,
			__in IFW1GlyphRenderStates *pGlyphRenderStates,
			__in IDWriteFactory *pDWriteFactory,
			__in const FW1_DWRITEFONTPARAMS *pDefaultFontParams,
			__out IFW1FontWrapper **ppFontWrapper
		) = 0;
		
		/// <summary>Create an IFW1GlyphVertexDrawer object.</summary>
		/// <remarks></remarks>
		/// <returns>Standard HRESULT error code.</returns>
		/// <param name="pDevice">The ID3D11Device that the vertex drawer will be used with.</param>
		/// <param name="VertexBufferSize">The size in bytes of the dynamic vertex buffer. An index buffer will be created with a matching size.</param>
		/// <param name="ppGlyphVertexDrawer">Address of a pointer to a glyph-vertex drawer (See IFW1GlyphVertexDrawer).</param>
		virtual HRESULT STDMETHODCALLTYPE CreateGlyphVertexDrawer(
			__in ID3D11Device *pDevice,
			__in UINT VertexBufferSize,
			__out IFW1GlyphVertexDrawer **ppGlyphVertexDrawer
		) = 0;
		
		/// <summary>Create an IFW1GlyphRenderStates object.</summary>
		/// <remarks></remarks>
		/// <returns>Standard HRESULT error code.</returns>
		/// <param name="pDevice">The ID3D11Device that the render-states will be used with.</param>
		/// <param name="DisableGeometryShader">If TRUE, no geometry shader will be created.</param>
		/// <param name="AnisotropicFiltering">If TRUE, a sampler state enabling anisotropic filtering will be created.</param>
		/// <param name="ppGlyphRenderStates">Address of a pointer to a glyph render-states object (See IFW1GlyphRenderStates).</param>
		virtual HRESULT STDMETHODCALLTYPE CreateGlyphRenderStates(
			__in ID3D11Device *pDevice,
			__in BOOL DisableGeometryShader,
			__in BOOL AnisotropicFiltering,
			__out IFW1GlyphRenderStates **ppGlyphRenderStates
		) = 0;
		
		/// <summary>Create an IFW1TextRenderer object.</summary>
		/// <remarks></remarks>
		/// <returns>Standard HRESULT error code.</returns>
		/// <param name="pGlyphProvider">The IFW1GlyphProvider that provides glyph-information for the text-renderer.</param>
		/// <param name="ppTextRenderer">Address of a pointer to a IFW1TextRenderer.</param>
		virtual HRESULT STDMETHODCALLTYPE CreateTextRenderer(
			__in IFW1GlyphProvider *pGlyphProvider,
			__out IFW1TextRenderer **ppTextRenderer
		) = 0;
		
		/// <summary>Create an IFW1TextGeometry object.</summary>
		/// <remarks></remarks>
		/// <returns>Standard HRESULT error code.</returns>
		/// <param name="ppTextGeometry">Address of a pointer to an IFW1TextGeometry.</param>
		virtual HRESULT STDMETHODCALLTYPE CreateTextGeometry(
			__out IFW1TextGeometry **ppTextGeometry
		) = 0;
		
		/// <summary>Create an IFW1GlyphProvider object.</summary>
		/// <remarks></remarks>
		/// <returns>Standard HRESULT error code.</returns>
		/// <param name="pGlyphAtlas">A glyph atlas to store glyph images in.</param>
		/// <param name="pDWriteFactory">A DirectWrite factory, used to create glyph render targets.</param>
		/// <param name="pFontCollection">A font collection used to identify fonts from DirectWrite font-face objects.</param>
		/// <param name="MaxGlyphWidth">The maximum width of a single glyph.</param>
		/// <param name="MaxGlyphHeight">The maximum height of a single glyph.</param>
		/// <param name="ppGlyphProvider">Address of a pointer to an IFW1GlyphProvider.</param>
		virtual HRESULT STDMETHODCALLTYPE CreateGlyphProvider(
			__in IFW1GlyphAtlas *pGlyphAtlas,
			__in IDWriteFactory *pDWriteFactory,
			__in IDWriteFontCollection *pFontCollection,
			__in UINT MaxGlyphWidth,
			__in UINT MaxGlyphHeight,
			__out IFW1GlyphProvider **ppGlyphProvider
		) = 0;
		
		/// <summary>Create an IFW1DWriteRenderTarget object.</summary>
		/// <remarks></remarks>
		/// <returns>Standard HRESULT error code.</returns>
		/// <param name="pDWriteFactory">A DirectWrite factory used to create the internal render target.</param>
		/// <param name="RenderTargetWidth">The width of the render target.</param>
		/// <param name="RenderTargetHeight">The height of the render target.</param>
		/// <param name="ppRenderTarget">Address of a pointer to an IFW1DWriteRenderTarget.</param>
		virtual HRESULT STDMETHODCALLTYPE CreateDWriteRenderTarget(
			__in IDWriteFactory *pDWriteFactory,
			__in UINT RenderTargetWidth,
			__in UINT RenderTargetHeight,
			__out IFW1DWriteRenderTarget **ppRenderTarget
		) = 0;
		
		/// <summary>Create an IFW1GlyphAtlas object.</summary>
		/// <remarks></remarks>
		/// <returns>Standard HRESULT error code.</returns>
		/// <param name="pDevice">A D3D11 device used to create device resources.</param>
		/// <param name="GlyphSheetWidth">Width of the atlas textures.</param>
		/// <param name="GlyphSheetHeight">Height of the atlas textures.</param>
		/// <param name="HardwareCoordBuffer">If TRUE, create a D3D11 buffer with glyph coordinates for each sheet, for use with the geometry shader.</param>
		/// <param name="AllowOversizedGlyph">If FALSE, glyphs that are larger than the atlas textures will be rejected instead of partially inserted.</param>
		/// <param name="MaxGlyphCountPerSheet">The maximum number of glyphs in a single sheet texture.</param>
		/// <param name="MipLevels">The number of mip levels for the textures.</param>
		/// <param name="MaxGlyphSheetCount">The maximum number of sheet textures.</param>
		/// <param name="ppGlyphAtlas">Address of a pointer to an IFW1GlyphAtlas.</param>
		virtual HRESULT STDMETHODCALLTYPE CreateGlyphAtlas(
			__in ID3D11Device *pDevice,
			__in UINT GlyphSheetWidth,
			__in UINT GlyphSheetHeight,
			__in BOOL HardwareCoordBuffer,
			__in BOOL AllowOversizedGlyph,
			__in UINT MaxGlyphCountPerSheet,
			__in UINT MipLevels,
			__in UINT MaxGlyphSheetCount,
			__out IFW1GlyphAtlas **ppGlyphAtlas
		) = 0;
		
		/// <summary>Create an IFW1GlyphSheet object.</summary>
		/// <remarks></remarks>
		/// <returns>Standard HRESULT error code.</returns>
		/// <param name="pDevice">A D3D11 device used to create device resources.</param>
		/// <param name="GlyphSheetWidth">Width of the sheet texture.</param>
		/// <param name="GlyphSheetHeight">Height of the sheet texture.</param>
		/// <param name="HardwareCoordBuffer">If TRUE, create a D3D11 buffer with glyph coordinates, for use with the geometry shader.</param>
		/// <param name="AllowOversizedGlyph">If FALSE, glyphs that are larger than the sheet texture will be rejected instead of partially inserted.</param>
		/// <param name="MaxGlyphCount">The maximum number of glyphs in the sheet.</param>
		/// <param name="MipLevels">The number of mip levels for the texture.</param>
		/// <param name="ppGlyphSheet">Address of a pointer to an IFW1GlyphSheet.</param>
		virtual HRESULT STDMETHODCALLTYPE CreateGlyphSheet(
			__in ID3D11Device *pDevice,
			__in UINT GlyphSheetWidth,
			__in UINT GlyphSheetHeight,
			__in BOOL HardwareCoordBuffer,
			__in BOOL AllowOversizedGlyph,
			__in UINT MaxGlyphCount,
			__in UINT MipLevels,
			__out IFW1GlyphSheet **ppGlyphSheet
		) = 0;
		
		/// <summary>Create an IFW1ColorRGBA object.</summary>
		/// <remarks>An IFW1ColorRGBA can be set as the drawing effect for a range in a DirectWrite text layout to override the default color.</remarks>
		/// <returns>Standard HRESULT error code.</returns>
		/// <param name="Color">The initial color, as 0xAaBbGgRr.</param>
		/// <param name="ppColor">Address of a pointer to an IFW1ColorRGBA.</param>
		virtual HRESULT STDMETHODCALLTYPE CreateColor(
			__in UINT32 Color,
			__out IFW1ColorRGBA **ppColor
		) = 0;
};

#ifdef FW1_COMPILETODLL
	extern "C" __declspec(dllexport) HRESULT STDMETHODCALLTYPE FW1CreateFactory(
		__in UINT32 Version,
		__out IFW1Factory **ppFactory
	);
#else
	/// <summary>The FW1CreateFactory method creates an IFWFactory object, that can subsequently be used to create any and all FW1 objects.</summary>
	/// <remarks></remarks>
	/// <returns>Standard HRESULT error code.</returns>
	/// <param name="Version">Set to FW1_VERSION. Is used to make sure the header matches the library version.</param>
	/// <param name="ppFactory">Address of a pointer to an IFW1Factory.</param>
	extern HRESULT STDMETHODCALLTYPE FW1CreateFactory(
		__in UINT32 Version,
		__out IFW1Factory **ppFactory
	);
#endif

typedef HRESULT (STDMETHODCALLTYPE * PFN_FW1CREATEFACTORY) (UINT32 Version, IFW1Factory **ppFactory);


#endif// IncludeGuard__FW1_FW1FontWrapper_h
