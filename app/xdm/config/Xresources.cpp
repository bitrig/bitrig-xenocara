! $Xorg: Xresources,v 1.3 2000/08/17 19:54:17 cpqbld Exp $
!
!
!
!
!


#define BS \ /* cpp can be trickier than m4 */
#define NLBS \n\ /* don't remove these comments */
xlogin*login.translations: #override BS
	Ctrl<Key>R: abort-display()NLBS
	<Key>F1: set-session-argument(failsafe) finish-field()NLBS
	<Key>Left: move-backward-character()NLBS
	<Key>Right: move-forward-character()NLBS
	<Key>Home: move-to-begining()NLBS
	<Key>End: move-to-end()NLBS
	Ctrl<Key>KP_Enter: set-session-argument(failsafe) finish-field()NLBS
	<Key>KP_Enter: set-session-argument() finish-field()NLBS
	Ctrl<Key>Return: set-session-argument(failsafe) finish-field()NLBS
	<Key>Return: set-session-argument() finish-field()

xlogin*greeting: Welcome to CLIENTHOST
xlogin*fail: Login incorrect or forbidden by policy

XHASHif WIDTH > 800
xlogin*greetFont: -adobe-helvetica-bold-o-normal--24-240-75-75-p-138-iso8859-1
xlogin*font: -adobe-helvetica-medium-r-normal--18-180-75-75-p-98-iso8859-1
xlogin*promptFont: -adobe-helvetica-bold-r-normal--18-180-75-75-p-103-iso8859-1
xlogin*failFont: -adobe-helvetica-bold-r-normal--18-180-75-75-p-103-iso8859-1
xlogin*greetFace:       DejaVu Sans-22:bold:italic:dpi=75
xlogin*face:            DejaVu Sans-16:dpi=75
xlogin*promptFace:      DejaVu Sans-16:bold:dpi=75
xlogin*failFace:        DejaVu Sans-16:bold:dpi=75
XHASHelse
xlogin*greetFont: -adobe-helvetica-bold-o-normal--17-120-100-100-p-92-iso8859-1
xlogin*font: -adobe-helvetica-medium-r-normal--12-120-75-75-p-67-iso8859-1
xlogin*promptFont: -adobe-helvetica-bold-r-normal--12-120-75-75-p-70-iso8859-1
xlogin*failFont: -adobe-helvetica-bold-o-normal--14-140-75-75-p-82-iso8859-1
xlogin*greetFace:       DejaVu Sans-18:bold:italic:dpi=75
xlogin*face:            DejaVu Sans-12:dpi=75
xlogin*promptFace:      DejaVu Sans-12:bold:dpi=75
xlogin*failFace:        DejaVu Sans-12:bold:dpi=75
XHASHendif

XHASHif !(defined(bpp1) || defined(bpp4) || defined(bpp8) || defined(bpp15))
XHASH if PLANES < 4
XHASH  define bpp1
XHASH elif PLANES > 8
XHASH  define bpp15
XHASH elif PLANES > 4
XHASH  define bpp8
XHASH else
XHASH  define bpp4
XHASH endif
XHASHendif  //**/* If manual override */**//

xlogin*borderWidth: 0
xlogin*frameWidth: 1
xlogin*innerFramesWidth: 1
xlogin*sepWidth: 0
xlogin*logoPadding: 0
xlogin*useShape: true

XHASHifndef bpp1
xlogin*greetColor: white
xlogin*background: #0d7340
xlogin*hiColor: #3d8f66
xlogin*shdColor: #0a5c33
xlogin*failColor: red
*Foreground: #fbfeff
XHASHelse
xlogin*greetColor: white
xlogin*background: black
xlogin*hiColor: white
xlogin*shdColor: white
xlogin*failColor: white
xlogin*promptColor: white
*Foreground: white
XHASHendif
XHASHif defined(bpp1)
xlogin*logoFileName: BITMAPDIR/**//Bitrig_1bpp.xpm
XHASHelif defined(bpp4)
xlogin*logoFileName: BITMAPDIR/**//Bitrig_4bpp.xpm
XHASHelif defined(bpp8)
xlogin*logoFileName: BITMAPDIR/**//Bitrig_8bpp.xpm
XHASHelif defined(bpp15)
xlogin*logoFileName: BITMAPDIR/**//Bitrig_15bpp.xpm
XHASHendif

! uncomment to disable logins
! xlogin.Login.allowRootLogin:	false

XHASHifndef bpp1
XConsole*background:	#0d7340
XHASHelse
XConsole*background:	black
XHASHendif
XConsole*foreground:	white
XConsole*cursorColor:	#0d7340
XConsole*borderWidth:	0
XConsole.text.geometry:	480x130
XConsole.verbose:	true
XConsole*iconic:	true
XConsole*font:		fixed
XConsole*scrollVertical: WhenNeeded

Chooser*geometry:		640x480
Chooser*allowShellResize:	false
Chooser*viewport.forceBars:	true

Chooser*label.font:	  -adobe-helvetica-bold-o-normal--24-*-p-*-iso8859-1
Chooser*label.label:	  XDMCP Host Menu from CLIENTHOST
Chooser*label.foreground: black
Chooser*list.font:	  lucidasanstypewriter-12
Chooser*Command.font:	  -adobe-helvetica-medium-r-normal--18-*-p-*-iso8859-1
