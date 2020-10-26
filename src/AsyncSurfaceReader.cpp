#include "AsyncSurfaceReader.h"
#include "cinder/gl/scoped.h"

AsyncSurfaceReader::AsyncSurfaceReader( const int width, const int height )
: mWidth( width )
, mHeight( height )
{
	ci::gl::Fbo::Format fboFmt;
	fboFmt.colorTexture( ci::gl::Texture::Format().loadTopDown( true ) );
	mFbo[0] = ci::gl::Fbo::create( width, height, fboFmt );
	mFbo[1] = ci::gl::Fbo::create( width, height, fboFmt );

	mPbo[0] = ci::gl::Pbo::create( GL_PIXEL_PACK_BUFFER, width * height * 4, 0, GL_STREAM_READ );
	mPbo[1] = ci::gl::Pbo::create( GL_PIXEL_PACK_BUFFER, width * height * 4, 0, GL_STREAM_READ );

	mSurface[0] = ci::Surface::create( width, height, true, ci::SurfaceChannelOrder::RGBA );
	mSurface[1] = ci::Surface::create( width, height, true, ci::SurfaceChannelOrder::RGBA );
}

ci::SurfaceRef AsyncSurfaceReader::readPixels()
{
	ci::gl::ScopedFramebuffer backFbo( mFbo[ mBackIndex ] );
	ci::gl::ScopedBuffer frontPbo( mPbo[ mFrontIndex ] );
	ci::gl::readBuffer( GL_COLOR_ATTACHMENT0 );
	ci::gl::readPixels( 0, 0, mWidth, mHeight, GL_RGBA, GL_UNSIGNED_BYTE, 0 );
	mPbo[ mBackIndex ]->getBufferSubData( 0, mWidth * mHeight * 4, mSurface[ mFrontIndex ]->getData() );
	return mSurface[ mFrontIndex ];
}

void AsyncSurfaceReader::bind()
{
	mFbo[ mFrontIndex ]->bindFramebuffer();
	ci::gl::pushMatrices();
}

void AsyncSurfaceReader::unbind()
{
	ci::gl::popMatrices();
	mFbo[ mFrontIndex ]->unbindFramebuffer();
	std::swap( mFrontIndex, mBackIndex );
}

ci::gl::TextureRef AsyncSurfaceReader::getTexture()
{
	return mFbo[mFrontIndex]->getColorTexture();	
}
