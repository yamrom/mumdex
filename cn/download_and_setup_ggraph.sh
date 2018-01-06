#! /bin/bash

# Genome selection
genomes=$@
if [ "$genomes" != "hg19" ] &&
    [ "$genomes" != "hg38" ] &&
    [ "$genomes" != "hg19 hg38" ] ; then
    echo Required command line arguments must be exactly \
        hg19, hg38, or hg19 hg38 1>&2
    exit 1
fi

echo This program will download, install, and setup G-Graph for you.
echo It will also download the "genome(s)" for \
    $genomes, annotations, and sample data.
echo Please contact Peter Andrews at paa@drpa.us if you encounter difficulties.
echo This has been tested on Linux and Mac.
echo

# Get and compile ggraph
if [ -e mumdex/ggraph ] ; then
    echo mumdex directory and ggraph program already exists - skipping 1>&2
else
    url=http://mumdex.com/mumdex.zip
    echo Downloading $url
    lwp-request -f $url > mumdex.zip
    echo Unzipping mumdex.zip
    unzip mumdex.zip > /dev/null
    rm mumdex.zip
    cd mumdex
    echo Compiling mumdex/ggraph
    make -j 4 ggraph > /dev/null
    if [ $? != 0 ] ; then
        echo Problem compiling ggraph - \
            please try to copile manually before continuing - quitting 1>&2
        exit 1
    fi
    cd ../
fi

# Get sample data
data="{m,f,d,s}.txt"
if [ -e s.txt ] ; then
    echo sample data already exists - skipping 1>&2
else
    for file in $(eval echo $data) ; do
        url=http://mumdex.com/ggraph/data/$file
        echo Downloading $url
        lwp-request -f $url > $file
    done
fi

# Get genomes
for genome in $genomes ; do
    gzip=$genome.fa.gz
    fasta=$genome.fa
    bin=$genome.fa.bin
    if [ -e $bin ] ; then
        echo Genome files for $genome already exist - skipping 1>&2
    else
        config="{knownGene,knownIsoforms,kgXref,cytoBand}.txt"
        for file in $(eval echo $config $gzip) ; do
            url=http://mumdex.com/ggraph/config/$genome/$file
            echo Downloading $url
            lwp-request -f $url > $file
        done
        echo Unzipping $gzip
        gunzip $gzip
        mkdir $bin
        eval mv $config $bin
    fi
done

echo 
echo Setup all done!
for genome in $genomes ; do
    echo
    echo You can now run G-Graph with $genome using the following command:
    echo
    echo ./mumdex/ggraph cn $genome.fa abspos,ratio,seg 0,1 "$data"
    if [ ! -e $genome.fa.bin/ref.seq.bin ] ; then
        echo
        echo Note the first time the command runs it will take a short time \
            to create a binary reference cache file
    fi
    if [ $genome = hg38 ] ; then
        echo 
        echo Note the sample data is processed for hg19 \
            but it will still display "(slightly incorrectly)" for hg38
    fi
done
