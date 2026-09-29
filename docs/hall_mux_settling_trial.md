# Essai diagnostique de stabilisation du mux Hall

TIM6 déclenche les ADC toutes les 50 µs (prescaler 239, période 49,
horloge timer 240 MHz). Les captures précédentes confirment 50 µs par
conversion, avec 350 µs entre deux triplets admis consécutifs en régime
normal. Le firmware change l'adresse du mux après le traitement du triplet
admis, puis rejette six paires avant d'utiliser la septième. L'ancien
intervalle nominal entre changement de mux et conversion retenue est donc
de 300 à 350 µs, selon la phase exacte du changement par rapport au
déclenchement TIM6 et la latence du callback. La borne basse correspond à
six périodes entières rejetées; les interruptions peuvent allonger cet
intervalle. Le temps exact jusqu'au début de l'échantillonnage ADC ne figure
pas dans les dumps existants.

Cet essai rejette douze paires, sans changer le timer, les seuils ni la
décision RELEASE. La treizième conversion est utilisée : l'intervalle
nominal devient 600 à 650 µs, soit 300 µs de marge supplémentaire. Le tour
des huit canaux passe nominalement de 2,8 à 5,2 ms. Si les faux RELEASE
disparaissent à matériel et geste identiques, cela oriente vers une
dépendance au temps de stabilisation du mux ou de l'acquisition; ce seul
résultat ne prouve pas lequel. Si le défaut persiste, comparer les nouvelles
captures Hall aux précédentes, en particulier la montée brute avant RELEASE.
