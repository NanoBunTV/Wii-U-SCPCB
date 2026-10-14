// Wii U gxLight : a Direct3D 7 style light (directional / point / spot), evaluated per vertex in the scene shader.
#ifndef GXLIGHT_H
#define GXLIGHT_H

class gxScene;

class gxLight{
public:
	gxLight( gxScene *scene,int type );
	~gxLight();

	int type;
	float diffuse[3];
	float position[3];		// world space
	float direction[3];		// world space
	float atten1;			// D3D linear attenuation (1/range)
	float theta,phi;		// spot cone (inner, outer), radians

private:
	gxScene *scene;

	/***** GX INTERFACE *****/
public:
	enum{
		LIGHT_DISTANT=1,LIGHT_POINT=2,LIGHT_SPOT=3
	};
	void setRange( float range );
	void setColor( const float rgb[3] ){ diffuse[0]=rgb[0];diffuse[1]=rgb[1];diffuse[2]=rgb[2]; }
	void setPosition( const float pos[3] );
	void setDirection( const float dir[3] );
	void setConeAngles( float inner,float outer );

	void getColor( float rgb[3] ){ rgb[0]=diffuse[0];rgb[1]=diffuse[1];rgb[2]=diffuse[2]; }
};

#endif
