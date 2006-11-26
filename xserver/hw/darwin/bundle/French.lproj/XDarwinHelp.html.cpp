<!-- $XFree86: xc/programs/Xserver/hw/darwin/bundle/English.lproj/XDarwinHelp.html.cpp,v 1.2 2001/11/04 07:02:28 torrey Exp $ -->

<html>
<head><META HTTP-EQUIV="content-type" CONTENT="text/html; charset=iso-8859-1">
<title>XDarwin Help</title>
</head>
<body>
<center>
    <h1>XDarwin X Server pour Mac OS X</h1>
    X_VENDOR_NAME X_VERSION<br>
    Date : X_REL_DATE
</center>
<h2>Sommaire</h2>
<ol>
    <li><A HREF="#notice">Avertissement</A></li>
    <li><A HREF="#usage">Utilisation</A></li>
    <li><A HREF="#path">Chemins d'acc�s</A></li>
    <li><A HREF="#prefs">Pr�f�rences</A></li>
    <li><A HREF="#license">Licence</A></li>
</ol>
<center>
    <h2><a NAME="notice">Avertissement</a></h2>
</center>
<blockquote>
#if PRE_RELEASE
Ceci est une pr�-version de XDarwin et ne fait par cons�quent l'objet d'aucun support client. Les bogues peuvent �tre signal�s et des patches peuvent �tre soumis sur la 
<A HREF="http://sourceforge.net/projects/xonx/">page du projet XonX</A> chez SourceForge. Veuillez prendre connaissance de la derni�re version sur <A HREF="http://sourceforge.net/projects/xonx/">XonX</A> ou le X_VENDOR_LINK avant de signaler un bogue d'une pr�-version.
#else
Si le serveur date de plus de 6-12 mois ou si votre mat�riel est plus r�cent que la date indiqu�e ci-dessus, veuillez vous procurer une version plus r�cente avant de signaler toute anomalie. Les bogues peuvent �tre signal�s et des patches peuvent �tre soumis sur la <A HREF="http://sourceforge.net/projects/xonx/">page du projet XonX</A> chez SourceForge.
#endif
</blockquote>
<blockquote>
Ce logiciel est distribu� sous la 
<A HREF="#license">Licence du Consortium X/X11 du MIT</A> et est fourni TEL QUEL, sans garanties. Veuillez prendre connaissance de la <A HREF="#license">Licence</A> avant toute utilisation.</blockquote>

<h2><a NAME="usage">Utilisation</a></h2>
<p>XDarwin est une X server libre et distribuable sans contrainte du <a HREF
="http://www.x.org/">X Window System</a>. This version of XDarwin was produced by the X_VENDOR_LINK. XDarwin fonctionne sous Mac OS X en mode � rootless � ou plein �cran.</p>
<p>Lorsque le syst�me X window est actif en mode plein �cran, il prend en charge la totalit� de l'�cran. Il est possible de revenir sur le bureau de Mac OS X en appuyant sur Commande-Option-A. Cette combinaison de touches peut �tre modifi�e dans les pr�f�rences. Pour revenir dans X window, cliquer sur l'ic�ne de XDarwin dans le Dock de Mac OS X.  (Un r�glage des pr�f�rences permet d'effectuer cette op�ration en cliquant dans une fen�tre flottante au lieu de l'ic�ne du Dock)</p>
<p>En mode � rootless �, X window system et Aqua utilisent le m�me affichage. La fen�tre-m�re de l'affichage X11 est de la taille de l'�cran et contient toutes les autre fen�tres. En mode � rootless � cette fen�tre-m�re n'est pas affich�e car Aqua g�re le fond d'�cran.</p>
<h3>�mulation de souris � plusieurs boutons</h3>
<p>Le fonctionnement de la plupart des applications X11 repose sur l'utilisation d'une souris � 3 boutons. Il est possible d'�muler une souris � 3 boutons avec un seul bouton en appuyant sur des touches de modification. Ceci est r�gl� dans la section "�mulation de souris � plusieurs boutons" de l'onglet "G�n�ral" des pr�f�rences. L'�mulation est activ�e par d�faut. Dans ce cas, cliquer en appuyant simultan�ment sur la touche "commande" simulera le bouton du milieu. Cliquer en appuyant simultan�ment sur la touche "option" simulera le bouton de droite. Les pr�f�rences permettent de r�gler n'importe quelle combinaison de touches de modification pour �muler les boutons du milieu et de droite. Notez que m�me si les touches de modifications sont mises en correspondance avec d'autres touches par xmodmap, ce sont les touches originelles sp�cifi�es dans les pr�f�rences qui assureront l'�mulation d'une souris � plusieurs boutons.

<h2><a NAME="path">R�glage du chemin d'acc�s</a></h2>
<p>Le chemin d'acc�s est une liste de r�pertoires utilis�s pour la recherche d'ex�cutables. Les commandes X11 sont situ�es dans <code>/usr/X11R6/bin</code>, qui doit �tre ajout� � votre chemin d'acc�s. XDarwin fait cela par d�faut, et peut �galement ajouter d'autres r�pertoires dans lesquels vous auriez install� d'autre commandes unix.</p>
<p>Les utilisateurs plus exp�riment�s auront d�j� r�gl� leur chemin d'acc�s correctement par le biais des fichiers d'initialisation de leur shell. Dans ce cas, il est possible de demander � XDarwin de ne pas modifier le chemin d'acc�s initial. XDarwin lance les premiers clients X11 dans le shell d'ouverture de session par d�faut. (Un shell de remplacement peut �tre sp�cifi� dans les pr�f�rences.) La fa�on de r�gler le chemin d'acc�s d�pend du shell utilis�. Ceci est document� dans les pages "man" du shell.</p>
<p>De plus, il est possible d'ajouter les pages "man" de X11 � la liste des pages recherch�es pour la documentation "man". Les pages "man" X11 se trouvent dans <code>/usr/X11R6/man</code>  et la variable d'environnement <code>MANPATH</code> contient la liste des r�pertoires dans lesquels chercher.</p>


<h2><a NAME="prefs">Pr�f�rences</a></h2>
<p>Un certain nombre d'options peuvent �tre r�gl�es dans les pr�f�rences. On acc�de aux pr�f�rences en choisissant "Pr�f�rences..." dans le menu "XDarwin". Les options d�crites comme options de d�marrage ne prendront pas effet avant le red�marrage de XDarwin. Les autres options prennent imm�diatement effet. Les diff�rentes options sont d�taill�es ci-apr�s :</p>
<h3>G�n�ral</h3>
<ul>
    <li><b>Utiliser le bip d'alerte Syst�me dans X11 :</b> Cocher cette option pour que le son d'alerte standard de Mac OS X soit utilis� � la place du son d'alerte de X11. L'option n'est pas coch�e ar d�faut. Dans ce cas, un simple signal sonore est utilis�.</li>
    <li><b>Autoriser X11 � changer la vitesse de la souris :</b> Dans une impl�mentation classique du syt�me X window, le gestionnaire de fen�tres peut modifier la vitesse de la souris. Cela peut s'av�rer d�routant puisque le r�glage de la vitesse de la souris peut �tre diff�rent dans les pr�f�rences de Mac OS X et dans le gestionnaire X window. Par d�faut, X11 n'est pas autoris� � changer la vitesse de la souris.</li>
    <li><b>�mulation de souris � plusieurs boutons :</b> Ceci est d�crit ci-dessus � la rubrique <a HREF="#usage">Usage</a>. Lorsque l'�mulation est activ�e, il suffit d'appuyer simultan�ment sur les touches modificatrices s�lectionn�es et sur le bouton de la souris afin d'�muler les boutons du milieu et de droite.</li>
</ul>
<h3>D�marrage</h3>
<ul>
    <li><b>Mode par d�faut :</b> Le mode sp�cifi� � cet endroit sera utilis� si l'utilisateur ne l'indique pas au d�marrage.</li>
    <li><b>Choix du mode d'affichage au d�marrage</b> Par d�faut, une fen�tre de dialogue est affich�e au d�marrage de XDarwin pour permettre � l'utilisateur de choisir entre le mode plein �cran et le mode � rootless �. Si cette option est d�sactiv�e, le mode par d�faut sera automatiquement utilis�.</li>
    <li><b>Num�ro d'affichage (Display)</b> X11 offre la possibilit� de plusieurs serveurs X sur un ordinateur. L'utilisateur doit sp�cifier ici le num�ro d'affichage utilis� par XDarwin dans le cas o� plusieurs serveurs X seraient en service simultan�ment.</li>
    <li><b>Autoriser la prise en charge Xinerama de plusieurs moniteurs :</b> XDarwin peut �tre utilis� avec plusieurs moniteur avec Xinerama, qui consid�re les diff�rents moniteurs comme des parties d'un �cran rectugulaire plus grand. Cette option permet de d�sactiver Xinerama mais XDarwin ne prend alors pour l'instant pas correctement en charge l'affichage sur plusieurs �crans. Si il n'y a qu'un seul moniteur, Xinerama est automatiquement d�sactiv�.</li>
    <li><b>Fichier clavier :</b> Un fichier de correspondance de clavier est lu au d�marrage puis transform� en un fihcier de correspondance clavier pour X11. Les fichiers de correspondance clavier, disponibles pour de nombreuses langues, se trouvent dans <code>/System/Library/Keyboards</code>.</li>
    <li><b>D�marrage des premiers clients X11 :</b> Lorsque XDarwin est d�marr� � partir du Finder, il lance <code>xinit</code> qui lance � son tour le gestionnaire X window ainsi que d'autres clients X. (Voir "<code>man xinit</code>" pour plus d'informations.) Avant de lancer <code>xinit</code>, XDarwin ajoute les r�pertoires ainsi sp�cifi�s au chemin d'acc�s de l'utilisateur. Par d�faut, seul <code>/usr/X11R6/bin</code> est ajout�. Il est possible d'ajouter d'autres r�pertoires en les s�parants � l'aide de deux points (<code>:</code>). Les clients X sont d�marr�s � partir du shell par d�faut de l'utilisateur. Ainsi, le fichier d'initialisation de shell de l'utilisateur est lu. Un autre shell peut �ventuellement �tre sp�cifi�.</li>
</ul>
<h3>Plein �cran</h3>
<ul>
    <li><b>Combinaison de touches :</b> Appuyer sur ce bouton, puis appuyer sur une ou plusieurs touches modificatrices suivies d'une touche ordinaire. Cette combinaison de touche servira � commuter entre Aqua et X11.</li>
    <li><b>Basculer dans X11 en cliquant sur l'ic�ne du Dock :</b> Cette option permet de passer dans X11 en cliquant dans l'ic�ne de XDarwin dans le Dock. Sur certaines versions de Mac OS X, la commutation en utilisant le Dock peut faire dispara�tre le curseur lors du retour dans Aqua.</li>
    <li><b>Afficher l'aide du mode plein �cran au d�marrage :</b> Permet l'affichage d'une fen�tre d'introduction lorsque XDarwin est d�marr� en mode plein �cran.</li>
    <li><b>Profondeur de couleur :</b> En mode plein �cran, l'affichage X11 peut utiliser une autre profondeur de couleur que celle employ�e par Aqua. Si "Actuelle" est choisi, XDarwin utilisera la m�me profondeur de couleur qu'Aqua. Les autres choix sont 8 (256 couleurs), 15 (milliers de couleurs) et 24 bits (millions de couleurs). </li>
</ul>

<h2><a NAME="license">Licence</a></h2>
The main license for XDarwin is one based on the traditional MIT X11 / X Consortium License, which does not impose any conditions on modification or redistribution of source code or binaries other than requiring that copyright/license notices are left intact. For more information and additional copyright/licensing notices covering some sections of the code, please refer to the source code.
<H3><A NAME="3"></A>X Consortium License</H3>
<p>Copyright (C) 1996 X Consortium</p>
<p>Permission is hereby granted, free of charge, to any person obtaining a 
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without
limitation the rights to use, copy, modify, merge, publish, distribute,
sublicense, and/or sell copies of the Software, and to permit persons to
whom the Software is furnished to do so, subject to the following conditions:</p>
<p>The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.</p>
<p>THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT
SHALL THE X CONSORTIUM BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
IN THE SOFTWARE.</p>
<p>Except as contained in this notice, the name of the X Consortium shall
not be used in advertising or otherwise to promote the sale, use or
other dealings in this Software without prior written authorization from
the X Consortium.</p>
<p>X Window System is a trademark of X Consortium, Inc.</p>
</body>
</html>

