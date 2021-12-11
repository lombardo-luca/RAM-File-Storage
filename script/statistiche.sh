#!/bin/bash

LOGFILE=../logs/logs.txt

# controllo se il file di log e' presente
if [ ! -f $LOGFILE ]; then
    LOGFILE=logs/logs.txt

    if [ ! -f $LOGFILE ]; then
    echo "File di log non trovato!"
    exit 1
	fi
fi

READS=0
NREADS=0
SENT=0
WRITES=0
LOCKS=0
OPENLOCKS=0
UNLOCKS=0
CLOSE=0
MAXCLIENT=0
CLIENT=0
SENTSIZE=0
WRITESIZE=0
TPOOLSIZE=0

MAXSIZE=0
MAXFILES=0
CACHEMISS=0

# array che conterra' il numero di richieste servite da ogni thread worker
T=()

# leggo il logFile riga per riga
while IFS= read -r line; do
	if [[ $line == *"threadpool"* ]]; then
		FORM="${line#*dimensione }"
		TPOOLSIZE="${FORM%%.*}"
		# inizializzo l'array dei thread worker
		for ((i = 0; i < TPOOLSIZE; i++)); do
			T[$i]=0
		done

    elif [[ $line == *"readFile"* ]]; then
    	READS=$(echo $READS + 1 | bc -l)

    elif [[ $line == *"readNFiles"* ]]; then
    	NREADS=$(echo $NREADS + 1 | bc -l)

    elif [[ $line == *"stato inviato"* ]]; then
    	SENT=$(echo $SENT + 1 | bc -l)
    	FORM="${line#*dimensione }"
    	SIZE="${FORM%%B*}"
    	SENTSIZE=$(echo $SENTSIZE + $SIZE | bc -l)
    	
    elif [[ $line == *"writeFile"* ]]; then
    	WRITES=$(echo $WRITES + 1 | bc -l)
    	FORM="${line#*dimensione }"
    	SIZE="${FORM%%B*}"
    	WRITESIZE=$(echo $WRITESIZE + $SIZE | bc -l)

   	elif [[ $line == *"una lockFile"* ]]; then
    	LOCKS=$(echo $LOCKS + 1 | bc -l)

    elif [[ $line == *"openFile (flags = 2)"* || $line == *"openFile (flags = 3)"* ]]; then
    	OPENLOCKS=$(echo $OPENLOCKS + 1 | bc -l)

    elif [[ $line == *"unlockFile"* ]]; then
    	UNLOCKS=$(echo $UNLOCKS + 1 | bc -l)

    elif [[ $line == *"closeFile"* ]]; then
    	CLOSE=$(echo $CLOSE + 1 | bc -l)

    elif [[ $line == *"Il thread"* ]]; then
    	FORM="${line#*thread }"
    	THREAD="${FORM%% ha*}"
    	T[$THREAD]=$(echo ${T[$THREAD]} + 1 | bc -l)

    elif [[ $line == *"Nuovo client"* ]]; then
    	CLIENT=$(echo $CLIENT + 1 | bc -l)
    	[[ $CLIENT -gt MAXCLIENT ]] && MAXCLIENT=$CLIENT

    elif [[ $line == *"Chiusa connessione"* ]]; then
    	CLIENT=$(echo $CLIENT - 1 | bc -l)

    elif [[ $line == *"Dimensione massima"* ]]; then
    	FORM="${line#*storage: }"
		MAXSIZE="${FORM%%MB*}"

	elif [[ $line == *"Numero massimo di file"* ]]; then
		FORM="${line#*server: }"
		MAXFILES="${FORM%%.*}"

	elif [[ $line == *"capacity misses"* ]]; then
		FORM="${line#*cache: }"
		CACHEMISS="${FORM%%.*}"
    fi
done < $LOGFILE

SENTAVG=$(echo $SENTSIZE / $SENT | bc -l | xargs printf "%.0f")		# calcolo la media aritmetica delle dimensioni dei file 
LETTURA=$(echo $READS + $NREADS | bc -l)

echo "Operazioni di lettura richieste: $LETTURA ($READS read, $NREADS readN)"
echo "Numero di files inviati (letture piu' espulsioni): $SENT, dimensione media: $SENTAVG B"
echo "Operazioni di write richieste: $WRITES, dimensione media: $WRITESIZE B"
echo "Operazioni di lock richieste: $LOCKS"
echo "Operazioni di open-lock richieste: $OPENLOCKS"
echo "Operazioni di unlock richieste: $UNLOCKS"
echo "Operazioni di close richieste: $CLOSE"
echo "Dimensione massima raggiunta dallo storage: $MAXSIZE MB"
echo "Massimo numero di file raggiunto dallo storage: $MAXFILES"
echo "Numero di capacity misses nella cache: $CACHEMISS"

# stampo le richieste servite da ogni thread worker
for ((i = 0; i < TPOOLSIZE; i++)); do
	echo "Richieste servite dal worker thread $i: ${T[$i]}"
done

echo "Massimo numero di connessioni contemporanee: $MAXCLIENT"

exit 0