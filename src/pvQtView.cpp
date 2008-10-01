/* pvQtView.cpp for freepvQt  08Sep2008 TKS
*/
#include "pvQtView.h"
#include <QtOpenGL/QtOpenGL>
#include <GL/glext.h>
#include <GL/glut.h>
#include <cmath>

#ifndef Pi
#define Pi 3.141592654
#define DEG(r) ( 180.0 * (r) / Pi )
#define RAD(d) ( Pi * (d) / 180.0 )
#endif

/**** maximum projection angle at eye ****/ 
#define MAXPROJFOV  135.0


 pvQtView::pvQtView(QWidget *parent)
     : QGLWidget(parent)
 {
	 thePic = 0;
	 theScreen = 0;
	 textgt = 0;

     Width = Height = 400;
     initView();
 }

 pvQtView::~pvQtView()
 {
     makeCurrent();
     glDeleteLists(theScreen, 1);
 }

/**  GUI Interactions  **/

 QSize pvQtView::minimumSizeHint() const
 {
     return QSize(50, 50);
 }

 QSize pvQtView::sizeHint() const
 {
     return QSize(400, 400);
 }
 
 void pvQtView::mousePressEvent(QMouseEvent *event)
 {
     lastPos = event->pos();
 }

 void pvQtView::mouseMoveEvent(QMouseEvent *event)
 {
     int dx = event->x() - lastPos.x();
     int dy = event->y() - lastPos.y();


     lastPos = event->pos();
 }

 double pvQtView::normalizeAngle(int &iangle, double lwr, double upr)
 {
 /* iangle is in degrees * 16.  Return value in degrees.
    Reduces angle to a principal value in (-180:180] or [0:360) depending 
    on sign of lwr, then clips this to the limits [lwr:upr]
    The result is placed in iangle and returned as a double.
 */
 	if( lwr >= upr){
 		iangle = 0;
 		return 0;
	}
 	double a = iangle / 16.0;
 	if( lwr < 0 ){
 		while ( a <= -180.0 ) a += 360.0;
		while ( a > 180.0 ) a -= 360.0;
	} else {
 		while ( a < 0.0 ) a += 360.0;
		while ( a >= 360.0 ) a -= 360.0;		
	}
	if( a < lwr ) a = lwr;
	else if( a > upr ) a = upr;
	iangle = (int)( 16 * a );
	return a;

 }

 int pvQtView::iAngle( double deg ){
	 return int(16 * deg);
 }

 void pvQtView::step_pan( int dp ){
	 setPan( ipan + dp * panstep  );
 }

 void pvQtView::step_tilt( int dp ){
	 setTilt( itilt + dp * tiltstep );
 }

 void pvQtView::step_zoom( int dp ){
	 setZoom( izoom - dp * zoomstep );
 }

 void pvQtView::step_roll( int dp ){
	 setSpin( ispin - dp * spinstep );
 }

 void pvQtView::home_view(){
	panAngle = tiltAngle = spinAngle = 0;
	ipan = itilt = ispin = 0;
	updateGL();
	showview();
 }

 void pvQtView::step_dist( int dp ){
	 idist += dp * diststep;
	 setDist( idist );
 }

 void pvQtView::showview(){
	 QString s;
	 s.sprintf("Yaw %.1f  Pitch %.1f  Roll %.1f  Dist %.1f  vFOV %.1f",
		 panAngle, tiltAngle, spinAngle, eyeDistance, vFOV );
	 emit reportView( s );
 }

 void pvQtView::showPic( pvQtPic * pic )
 {
	if( pic != 0 && pic->Type() == pvQtPic::nil ) {
		qWarning("pvQtView::showPic: empty pvQtPic ignored" );
		pic = 0;
	}
	setupPic( pic );
 }

void pvQtView::picChanged( )
{
	updatePic();
}

/**  Field-of-View Policy

	User can adjust 'zoom' and the distance of the eye point 
	from the center of the spherical projection screen, as well
	as the size of the display window.  Zoom sets the viewing
	magnification at the center of the window.  Eye position
	changes the viewing projection without affecting central
	magnification.  When the window is resized, 
	
	Eye distance changes the viewing projection by moving the 
	actual eye point away from the sphere center: 
		angle at eye = angle on sphere / (eye distance + 1) 
	where eye distance is measured in sphere radii.  The OGL
	projection angle, wFOV, is the eye angle corresponding
	to vFOV, the vertical field of view on the sphere.

	The working parameters are display height in pixels, Height;
	eyeDistance, in sphere radii; and vFOV.  The zoom control
	adjusts vFOV directly. The working limits on vFOV depend
	on Height and eyeDistance, so vFOV may change when one of
	those changes.

	As a practical matter the maximum view angle at the eye is
	limited to 135 degrees, so that is the largest vFOV allowed
	when the eye is at sphere center (distance 0). As the eye
	moves out, more of the sphere becomes visible, up to a maximum
	of about 315 degrees at distance 1.08.  Beyond that point the
	visible area of the sphere shrinks again.

**/

void pvQtView::initView()
 {
 /* set the intial view parameters and the
	default user step increments 
*/
	 eyeDistance = 0.0; // spherical projection
	 minDist = 0.0; maxDist = 2.0;	// fixed limits
	 idist = 0; diststep = 20;	// % of radius

     minFOV = 5.0;  maxFOV = MAXPROJFOV;
     zoomstep = 5 * 16;	// 5 degrees in FOV
     setFOV( 90.0 );

     panstep = tiltstep = spinstep = 32;	// 2 degrees
	 home_view();	// zero rotation
}

void pvQtView::setFOV( double newvfov ){
 /* set
	vFOV = newvfov (clipped legal) = view height as an
		angle at the center of the unit sphere
	wFOV = half view height as an angle at the eye point.
  If newfov is zero or omitted, uses current vFOV.
  Note also sets constant view clipping distances.
*/
	if(newvfov == 0 ) newvfov = vFOV;
	if( newvfov < minFOV ) newvfov = minFOV;
	else if( newvfov > maxFOV) newvfov = maxFOV;
	
	vFOV = newvfov;
	izoom = iAngle( vFOV );	
	wFOV = vFOV / (eyeDistance + 1);

	Znear = 0.05; Zfar = 7;
	
}

void pvQtView::setDist( int id ){
/*  set distance of eye from sphere center,
	and adjust vFOV so that magnification at
	view center is unchanged.  
	Post new vFOV limits.
	Then update the screen.
*/
	double d = id / 100.0;
	if( d < minDist ) d = minDist;
	if( d > maxDist ) d = maxDist;
	idist = int( 100 * d );
	eyeDistance = d;

	double m = MAXPROJFOV * (d > 1 ? 2 : d + 1 );
	if( m < minFOV ) m = minFOV;
	maxFOV = m;
	setFOV( );
	updateGL();
	showview();
}

 void pvQtView::setPan(int angle)
 {
     panAngle = normalizeAngle(angle, -180, 180);
     if (angle != ipan) {
         ipan = angle;
         updateGL();
		 showview();
     }
 }

 void pvQtView::setTilt(int angle)
 {
     tiltAngle = normalizeAngle(angle, -90, 90);
     if (angle != itilt) {
         itilt = angle;
         updateGL();
		 showview();
     }
 }

 void pvQtView::setSpin(int angle)
 {
     spinAngle = normalizeAngle(angle, -180, 180);
     if (angle != ispin) {
         ispin = angle;
         updateGL();
		 showview();
     }
 }

 void pvQtView::setZoom(int angle)
 {
 	double a = normalizeAngle(angle, minFOV, maxFOV);
	if (angle != izoom) {
         izoom = angle;
         setFOV(a);
         updateGL();
		 showview();
     }
 }



 void pvQtView::initializeGL()
 {
	qglClearColor(Qt::black);
	glShadeModel(GL_SMOOTH);
	glEnable(GL_DEPTH_TEST);

	glEnable(GL_CULL_FACE);

 // create 2D and cube texture objects
	glGenTextures( 2, texIDs );
	glBindTexture(GL_TEXTURE_2D, texIDs[0] );
	glBindTexture(GL_TEXTURE_CUBE_MAP, texIDs[1] );
  // set texture mapping parameters...
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);

    glTexGenf( GL_S, GL_TEXTURE_GEN_MODE, GL_NORMAL_MAP );
	glTexGenf( GL_T, GL_TEXTURE_GEN_MODE, GL_NORMAL_MAP );
	glTexGenf( GL_R, GL_TEXTURE_GEN_MODE, GL_NORMAL_MAP );
    glEnable( GL_TEXTURE_GEN_S );
    glEnable( GL_TEXTURE_GEN_T );
    glEnable( GL_TEXTURE_GEN_R );

   // create projection screen displaylist
	theScreen = glGenLists(1);
	makeSphere( theScreen );
   
 }

 void pvQtView::paintGL()
 {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	if( textgt ){
		glEnable( textgt );
	}
 // eye position rotates, screen does not

	glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
	gluPerspective( wFOV, portAR, 
				    Znear,  Zfar);
	double D = eyeDistance;
    gluLookAt( -D, 0, 0,
	           0.01, 0, 0,
			   0, 0, 1 );
//	glTranslated( eyeDistance, 0, 0 );
	glFrontFace( GL_CW );

 // Treat rotations as Euler angles
	glRotated( spinAngle, 1, 0, 0 );     
	glRotated( tiltAngle, 0, 1, 0 );
	glRotated( panAngle, 0, 0, 1 );
	glMatrixMode(GL_MODELVIEW);

	glCallList(theScreen);
 }

 void pvQtView::resizeGL(int width, int height)
 {
// Viewport fills window.
	glViewport(0, 0, width, height);

	Width = width; Height = height;
	portAR = (double)Width / (double)Height;

 	showview();  // displays settings
 }
 

void pvQtView::makeSphere( GLuint list )
 {
	GLUquadricObj *qobj = gluNewQuadric();
	gluQuadricDrawStyle(qobj, textgt ? GLU_FILL : GLU_LINE);
	gluQuadricNormals(qobj, GLU_SMOOTH);
////	gluQuadricTexture(qobj, GL_TRUE );

	glNewList(list, GL_COMPILE);
		gluSphere(qobj, 1.0, 36, 35);
	glEndList();
 }


/* set up screen and texture maps for a picture
*/
void pvQtView::setupPic( pvQtPic * pic )
{
	static GLenum cubefaces[6] = {
		GL_TEXTURE_CUBE_MAP_NEGATIVE_Z,	// front
		GL_TEXTURE_CUBE_MAP_POSITIVE_X, // right
		GL_TEXTURE_CUBE_MAP_POSITIVE_Z,	// back
		GL_TEXTURE_CUBE_MAP_NEGATIVE_X,	// left
		GL_TEXTURE_CUBE_MAP_POSITIVE_Y,	// top
		GL_TEXTURE_CUBE_MAP_NEGATIVE_Y, // bottom
	};

	thePic = pic;
	makeCurrent();	// get OGL's attention
  // QImage row alignment
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

  // disable texture mapping
//	if( textgt ) glDisable( textgt );
//	textgt = 0;

	if( pic ){
		pvQtPic::PicType pictype = pic->Type();
		if( pictype == pvQtPic::cub ){
			textgt = GL_TEXTURE_CUBE_MAP;
			theTex = texIDs[1];
			for(int i = 0; i < 6; i++){
				QImage * p = pic->FaceImage(pvQtPic::PicFace(i));
				if( p ){
					glTexImage2D( cubefaces[i], 0, GL_RGB,
						p->width(), p->height(), 1, // no border?
						GL_RGB, GL_UNSIGNED_BYTE,
						p->data_ptr() );
					delete p;
				}
			}
		} else {
			textgt = GL_TEXTURE_2D;
			theTex = texIDs[0];
			QImage * p = pic->FaceImage(pvQtPic::PicFace(0));
			if( p ){
				glTexImage2D( textgt, 0, GL_RGB,
					p->width(), p->height(), 0, // no border?
					GL_RGB, GL_UNSIGNED_BYTE,
					p->data_ptr() );
				delete p;
			}
		}
	}

  // rebuild the display list
	makeSphere( theScreen );

	updateGL();
	showview();
}

void pvQtView::updatePic()
{
	setupPic( thePic );
}
