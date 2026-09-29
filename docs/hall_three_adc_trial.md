# Essai Hall avec un ADC par sortie

Le STM32H743 connecte PC1 (Hall A) a ADC1/2/3 canal 11, PA4 (Hall B) a
ADC1/2 canal 18, PA5 (Hall C) a ADC1/2 canal 19 et PB1 (Volume) a ADC1/2
canal 5. Source : [datasheet STM32H743, table des fonctions alternatives](https://www.st.com/resource/en/datasheet/stm32h743ig.pdf).

L'essai affecte A a ADC3, B a ADC2 et C a ADC1. Chacun a une seule
conversion reguliere declenchee par TIM6 TRGO et un DMA circulaire d'un
echantillon. ADC3 utilise DMA1 Stream6, requete ADC3 et son IRQ. L'adresse
mux, la cadence TIM6 et les six triplets rejetes apres chaque changement
de mux restent identiques. Les seuils et la decision RELEASE sont inchanges.

PB1 ne peut pas aller sur un quatrieme ADC. Volume reste donc exclu des
sequences pendant cet essai et sa valeur est fixee a 65535, comme dans
l'essai precedent sans rang Volume. L'essai est diagnostique, pas une
architecture de production avec potentiometre fonctionnel.

Pour lire la trace apres reproduction (4096 enregistrements de 88 octets) :

```gdb
shell cls
set pagination off
set logging file hall_three_adc_gdb.txt
set logging overwrite on
set logging enabled on
p/x *(unsigned int*)&g_hall_capture_sequence
p/x *(unsigned char*)&g_hall_capture_frozen
p/x *(unsigned int*)&g_hall_capture_held_mask
p/x &g_hall_capture
p/x &g_hall_capture_calibration
dump binary memory hall_three_adc.bin (char*)&g_hall_capture ((char*)&g_hall_capture+360448)
dump binary memory hall_calibration.bin (char*)&g_hall_capture_calibration ((char*)&g_hall_capture_calibration+192)
set logging enabled off
```

Le decodeur `tools/decode_hall_capture.ps1` accepte ce format et les anciens
formats de 56 et 72 octets. Pour le nouveau format, `raw_a` vient d'ADC3,
`raw_b` d'ADC2 et `raw_c` d'ADC1. Les champs `adc3_*` permettent de controler
son callback, sa generation mux et son NDTR lors d'une derive.
