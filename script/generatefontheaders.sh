#!/bin/bash

# Fail and exit on errors:
set -e

thisDir=$(pwd)
outDir="${thisDir}/lib/fonts"

fontconvert="${thisDir}/lib/Adafruit-GFX-Library/fontconvert/fontconvert"

if [ ! -f "$fontconvert" ]; then
	echo "fontconvert tool not found"
	exit 1
fi

fontFilePath=$(realpath "$1")
if [ ! -f "$fontFilePath" ]; then
	echo "Font file does not exist"
	exit 1
fi

charInput="$2"
if [[ $charInput =~ ^[0-9]+-[0-9]+$ ]]; then
	IFS='-'
	read -ra charRangeArray <<< "$charInput"
	firstchar="${charRangeArray[0]}"
	lastchar="${charRangeArray[1]}"
else
	IFS=','
	read -ra charArray <<< "$charInput"
fi

fontSizes="$3"
if [[ $fontSizes =~ [^0-9,] ]]; then
	echo "Font sizes must contain numbers and commas only. Example: ${0} ./path/to/font.ttf 32-127 10,12,14"
	exit 1
fi
# Set the delimiter then split string into an array:
IFS=','
read -ra fontSizesArray <<< "$fontSizes"

for fontSize in "${fontSizesArray[@]}"; do
	if [[ -n $charRangeArray ]]; then
		fontName="$(basename "${fontFilePath}" | cut -f 1 -d '.' | tr -s ' ' | tr ' ' '_' | tr '-' '_')_${fontSize}pt"
		fontNameLower=$(echo -n "${fontName}" | tr '[:upper:]' '[:lower:]')
		fontNameUpper=$(echo -n "${fontName}" | tr '[:lower:]' '[:upper:]')
		outFilePath="${outDir}/${fontNameLower}.h"
		headerGuard="OPAGO_FONTS_${fontNameUpper}_H"
		# Initialize the output file with the header guard
		echo "#ifndef ${headerGuard}" > ${outFilePath}
		echo "#define ${headerGuard}" >> ${outFilePath}
		echo "" >> ${outFilePath}
		${fontconvert} "${fontFilePath}" ${fontSize} ${firstchar} ${lastchar} >> ${outFilePath}
		# Close the header guard at the end of the file
		echo "#endif" >> ${outFilePath}
		echo "Created ${outFilePath}"
	else
		for char in "${charArray[@]}"; do
			fontName="$(basename "${fontFilePath}" | cut -f 1 -d '.' | tr -s ' ' | tr ' ' '_' | tr '-' '_')_${fontSize}pt_char$(printf "%x" ${char})"
			fontNameLower=$(echo -n "${fontName}" | tr '[:upper:]' '[:lower:]')
			fontNameUpper=$(echo -n "${fontName}" | tr '[:lower:]' '[:upper:]')
			outFilePath="${outDir}/${fontNameLower}.h"
			headerGuard="OPAGO_FONTS_${fontNameUpper}_H"
			# Initialize the output file with the header guard
			echo "#ifndef ${headerGuard}" > ${outFilePath}
			echo "#define ${headerGuard}" >> ${outFilePath}
			echo "" >> ${outFilePath}
			# Create a temporary copy of the font file with a unique name
			tempFontFilePath="${thisDir}/${fontName}.ttf"
			cp "${fontFilePath}" "${tempFontFilePath}"
			${fontconvert} "${tempFontFilePath}" ${fontSize} ${char} ${char} >> ${outFilePath}
			# Delete the temporary font file
			rm "${tempFontFilePath}"
			# Close the header guard at the end of the file
			echo "#endif" >> ${outFilePath}
			echo "Created ${outFilePath}"
		done
	fi
done