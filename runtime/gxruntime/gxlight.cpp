#include "std.h"
#include "gxlight.h"
#include "gxscene.h"

static const float HALFPI=1.57079632679f;

gxLight::gxLight( gxScene *s,int t ):scene(s){
	type=(t==LIGHT_POINT || t==LIGHT_SPOT) ? t : LIGHT_DISTANT;
	diffuse[0]=diffuse[1]=diffuse[2]=1;
	position[0]=position[1]=position[2]=0;
	direction[0]=direction[1]=0;direction[2]=1;
	theta=0;
	phi=HALFPI;
	setRange( 1000 );
}

gxLight::~gxLight(){
}

void gxLight::setRange( float r ){
	atten1=1.0f/r;
}

void gxLight::setPosition( const float pos[3] ){
	position[0]=pos[0];position[1]=pos[1];position[2]=pos[2];
}

void gxLight::setDirection( const float dir[3] ){
	direction[0]=dir[0];direction[1]=dir[1];direction[2]=dir[2];
}

void gxLight::setConeAngles( float inner,float outer ){
	theta=inner;
	phi=outer;
}
