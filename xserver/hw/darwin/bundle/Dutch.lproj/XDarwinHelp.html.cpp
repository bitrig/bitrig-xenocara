<!-- $XFree86: xc/programs/Xserver/hw/darwin/bundle/English.lproj/XDarwinHelp.html.cpp,v 1.2 2001/11/04 07:02:28 torrey Exp $ -->

<html>
<head>
<title>XDarwin Help</title>
</head>
<body>
<center>
    <h1>XDarwin X Server for Mac OS X</h1>
    X_VENDOR_NAME X_VERSION<br>
    Release Date: X_REL_DATE
</center>
<h2>Inhoud</h2>
<ol>
    <li><A HREF="#notice">Belangrijke Informatie</A></li>
    <li><A HREF="#usage">Gebruik</A></li>
    <li><A HREF="#path">Instellen van het Path</A></li>
    <li><A HREF="#prefs">Voorkeursinstellingen</A></li>
    <li><A HREF="#license">Licentie</A></li>
</ol>
<center>
    <h2><a NAME="notice">Belangrijke Informatie</a></h2>
</center>
<blockquote>
#if X_PRE_RELEASE
Dit is een pre-release van XDarwin, waarvoor geen ondersteuning beschikbaar is.  Rapporteren van bugs en aanleveren van patches kan op de <A HREF="http://sourceforge.net/projects/xonx/">XonX project pagina</A> bij SourceForge.  Kijk alvorens een bug te rapporteren in een pre-release eerst of een nieuwe versie beschikbaar is bij <A HREF="http://sourceforge.net/projects/xonx/">XonX</A> of de X_VENDOR_LINK.
#else
Als de server ouder is dan 6-12 maanden, of als uw hardware nieuwer is dan de bovenstaande datum, kijk dan of een nieuwe versie beschikbaar is voor u een probleem aanmeldt.  Rapporteren van bugs en aanleveren van patches kan op de <A HREF="http://sourceforge.net/projects/xonx/">XonX project pagina</A> bij SourceForge.
#endif
</blockquote>
<blockquote>
Deze software is beschikbaar gesteld onder de voorwaarden van de <A HREF="#license">MIT X11 / X Consortium Licentie</A> en is beschikbaar 'AS IS',zonder enige garantie. Lees s.v.p. de <A HREF="#license">Licentie</A> voor gebruik.</blockquote>

<h2><a NAME="usage">Gebruik</a></h2>
<p>XDarwin is een open-source X server van het <a HREF="http://www.x.org/">X Window Systeem</a>. This version of XDarwin was produced by the X_VENDOR_LINK.  XDarwin werkt op Mac OS X in schermvullende of rootless modus.</p>
<p>Het X window systeem in schermvullende modus neemt het hele beeldscherm in beslag. U schakelt terug naar de Mac OS X desktop door de toesten Command-Option-A in te drukken. Deze toetsencombinatie kunt u veranderen in de Voorkeuren. Op de Mac OS X desktop klikt u op de XDarwin icoon in de Dock om weer naar het X window systeem te schakelen.  (In de Voorkeuren kunt er voor kiezen om een apart XDarwin schakelpaneel te gebruiken op de Mac OS X desktop.)</p>
<p>In rootless modus verschijnen het X window systeem en Aqua (de Mac OS X desktop) tegelijk op het scherm. Het achtergrondscherm van X11, waarbinnen alle X11 vensters vallen, is net zo groot als het gehele scherm, maar het achtergrondscherm zelf is onzichtbaar.</p>

<h3>Meerknopsmuis emulatie</h3>
<p>Voor veel X11 programma's hebt u een 3-knops muis nodig.  Met een 1-knops muis kunt u een 3-knops muis nabootsen door een toets in te drukken terwijl u klikt met de muis.  Het instellen hiervan kan bij Voorkeuren, "Meerknopsmuis emulatie" in "Algemeen".  Emulatie is standaard ingeschakeld: ingedrukt houden van de "command" toets terwijl u klikt emuleert knop 2, ingedrukt houden van "option" emuleert knop 3.  Deze toetsen kunt u dus wijzigen in de Voorkeuren.  Let op: als u xmodmap gebruikt om de indeling van het toetsenbord te wijzigen, moet u toch de oorspronkelijke toetsen op het toetsenbord gebruiken voor deze functie.</p>

<h2><a NAME="path">Instellen van het Path</a></h2>
<p>Het path is de lijst van directories waarin gezocht wordt naar commando's.  De X11 commando's staan in de directory <code>/usr/X11R6/bin</code>, die dus aan uw path moet worden toegevoegd.  XDarwin doet dit automatisch voor u en kan extra directories toevoegen waarin u commando's hebt ge&iuml;nstalleerd.</p>

<p>Ervaren gebruikers zullen het path al correct hebben ingesteld in de configuratiebestanden voor hun shell.  In dat geval kunt u XDarwin via de Voorkeuren vertellen het path niet te wijzigen.  XDarwin start de eerste X11 clients binnen de standaard login shell van de gebruiker (bij de Voorkeuren kunt u een afwijkende shell opgeven).  Het instellen van het path is afhankelijk van de shell.  Zie hiervoor de man pages voor de shell.</p>

<p>Het kan handig zijn de manualpages voor X11 toe te voegen aan de lijst waarin gezocht wordt als u documentatie opvraagt.  De manualpages voor X11 staan in <code>/usr/X11R6/man</code> en de <code>MANPATH</code> environment variable bevat de lijst van directories waarin naar documentatie wordt gezocht.</p>

<h2><a NAME="prefs">Voorkeursinstellingen</a></h2>
<p>Een aantal instellingen kan worden gewijzigd door "Voorkeuren..." te kiezen in het "XDarwin" menu.  Wijzigingen van de instellingen genoemd onder "Start" gaan pas in als u XDarwin opnieuw hebt gestart.  Een wijziging van de overige instellingen is direct effectief.  Hier onder vindt u de verschillende mogelijkheden beschreven:</p>

<h3>Algemeen</h3>
<ul>
    <li><b>Gebruik systeempiep voor X11:</b> Als u dit inschakelt wordt het Mac OS X  waarschuwingssignaal ook gebruikt door X11, anders gebruikt X11 een simpele pieptoon (dit is de standaardinstelling).</li>
    <li><b>Wijzigen muis-versnelling door X11 mogelijk:</b> In een standaard X window systeem kan de window manager de muis-versnelling aanpassen.  Dit kan verwarrend zijn omdat de snelheid onder X11 dan verschillend kan zijn van de snelheid die u in Mac OS X bij Systeemvoorkeuren hebt ingesteld.  Om verwarring te voorkomen is de standaardinstelling dat X11 de versnelling niet kan wijzigen.</li>
    <li><b>Meerknopsmuis emulatie:</b> Dit is hierboven beschreven bij <a HREF="#usage">Gebruik</a>.  Als emulatie is ingeschakeld moet u de gekozen toetsen ingedrukt houden terwijl u met de muis klikt om de tweede en derde muisknop na te bootsen.</li>
</ul>

<h3>Start</h3>
<ul>
    <li><b>Standaard modus:</b> Hier kiest u de standaard scherm-modus: schermvullend of rootless (hierboven beschreven bij <a HREF="#usage">Gebruik</a>).  U kunt ook kiezen tijdens het starten van XDarwin, zie de optie hieronder.</li>
    <li><b>Kies scherm-modus tijdens start:</b> Dit is standaard ingeschakeld zodat u tijdens het starten van XDarwin kunt kiezen tussen schermvullend en rootless scherm-modus.  Als u dit uitschakelt start XDarwin in de standaard modus zonder u iets te vragen.</li>
    <li><b>X11 scherm nummer:</b> Met X11 kunnen meerdere schermen worden aangestuurd door verschillende X servers op dezelfde computer.  Als u meerdere X servers tegelijk wilt gebruiken stelt u hier het scherm nummer in dat door XDarwin wordt gebruikt.</li>
    <li><b>Xinerama multi-monitor ondersteuning mogelijk:</b> XDarwin ondersteunt het gebruik van meerdere monitoren met Xinerama, waarbij elke monitor wordt gezien als deel van &eacute;&eacute;n groot rechthoekig scherm.  U kunt Xinerama hier uitschakelen, maar XDarwin werkt op dit moment zonder Xinerama niet goed met meerdere monitoren.  Als u maar 1 monitor gebruikt is deze instelling automatisch uitgeschakeld.</li>
    <li><b>Toetsenbordindeling-bestand:</b> Een toetsenbordindeling-bestand wordt bij het starten geladen en omgezet naar een X11 toetsenbordindeling.  Voor verschillende talen vindt u toetsenbordindelingen in de directory <code>/System/Library/Keyboards</code>.</li>
    <li><b>Bij starten eerste X11 clients:</b> Als XDarwin start, wordt  <code>xinit</code> uitgevoerd om de X window manager en andere X clients te starten (zie "<code>man xinit</code>").  Voordat XDarwin <code>xinit</code> uitvoert voegt het de opgegeven directories toe aan het path.  Standaard wordt alleen <code>/usr/X11R6/bin</code> toegevoegd.  U kunt meerdere directories opgeven, gescheiden door een dubbelepunt.  X clients worden gestart met de standaard login shell van de gebruiker met gebruik van de configuratiebestanden voor die shell.  U kunt een afwijkende shell opgeven.</li>
</ul>

<h3>Schermvullend</h3>
<ul>
    <li><b>Toetscombinatie knop:</b> Klik op deze knop om de toetscombinatie te wijzigen waarmee u tussen de Mac OS X desktop en X11 schakelt.  Als toetscombinatie kunt u elke combinatie gebruiken van de shift, control, command en option toetsen samen met &eacute;&eacute;n normale toets.</li>
    <li><b>Klikken op icoon in Dock schakelt naar X11:</b> Hiermee is een klik op de XDarwin icoon in de Dock voldoende om naar X11 te schakelen.  In sommige versies van Mac OS X verdwijnt soms de cursor als u deze mogelijkheid gebruikt en daarna terugkeert naar de Mac OS X desktop.</li>
    <li><b>Toon help bij schermvullend starten:</b> Hiermee wordt een inleidend scherm getoond als XDarwin schermvullend start.</li>
    <li><b>Kleurdiepte:</b> In de schermvullende modus kan X11 een andere kleurdiepte gebruiken dan Aqua (en de Mac OS X desktop).  Als u "Huidig" kiest, neemt XDarwin bij het starten de kleurdiepte over van Aqua.  U kunt ook kiezen voor 8, 15 of 24 bits.</li>
</ul>

<h2><a NAME="license">Licentie</a></h2>
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

