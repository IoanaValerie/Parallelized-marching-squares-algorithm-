Nume: Ciobotea Ioana
Grupa: 333CC
Tema1: APD

 Am paralelizat functiile rescale, sample_grid si march din codul intial intr-o
functie noua pe care o apelez la creearea thread-urilor (thread_function) pentru a accelera prelucrarea imaginilor.

 Am folosit structura ThreadData pentru a transmite date necesare functiei
thread_function, nefiind permis sa am variabile globale.

Paralelizari:
1) Rescale: am paralelizat interpolarea bicubica folosita in algoritmul
secvential impartind imaginea in segmente, fiecare thread fiind responsabil de
o parte a imaginii finale.

2) Sample_grid: imaginea redimensionata este impartita in segmente
mai mici, fiecare fiind prelucrat de un thread.

3) March: fiecare thread lucreaza pe regiuni distincte ale grid-ului pentru a se 
executa mai rapid acest pas al algoritmului.

Bariere:
Am folosit bariere dupa fiecare paralelizare facuta pentru a ma asigura ca
toate thread-urile au finalizat sarcina data inainte de a trece la urmatorul pas,
pentru a nu modifica in acelasi timp aceeasi parte a imaginii (sincronizare).

