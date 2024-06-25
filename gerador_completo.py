"""
Tecniquim 0x00
"""
from pathlib import Path

import py5

posters_path = Path.cwd() / 'posters'


def setup():
    global verso, tiragem, f, linhas, posters
    py5.size(1587, 1122)
    tiragem = 3
    # precisa ter Inconsolata Bold instalada
    f = py5.create_font('Inconsolata Bold', 12)
    py5.no_loop()

    posters = [py5.load_shape(p)
              for p in list(posters_path.iterdir())[:tiragem]]

    linhas = py5.load_strings('horoscopos.txt')
    print(linhas)

def draw():
    pdf = py5.create_graphics(int(py5.width * 0.75), int(py5.height * 0.75),
                              py5.PDF, "Tecniquim0-{}.pdf".
                              format(tiragem))
    py5.begin_record(pdf)
    py5.no_fill()
    for i in range(tiragem):
        pdf.scale(0.75)
        #py5.background(255)
        py5.fill(0)
        py5.text_font(f)
        texto = quebra_frase(linhas[i], 200)
        py5.text(texto, 1290, 660)
        #py5.shape(frente)
        pdf.next_page()
        pdf.scale(2)
        py5.background(255)
        py5.shape(posters[i], 60, 40, 1.15, 1.15)
        py5.text_size(11)
        #py5.text('generate({})'.format(rnd_seed), 10, 10)
    
        if i < tiragem - 1:
            pdf.next_page()

    py5.end_record()
    py5.exit_sketch()


def quebra_frase(frase, largura):
    resultado = ""
    parcial = ""
    for letra in frase:
        parcial += letra
        if py5.text_width(parcial) > largura:
            ultimo_espaco = parcial.rfind(' ')
            resultado += '\n' + parcial[:ultimo_espaco]
            parcial = parcial[ultimo_espaco + 1:]
    resultado += '\n' + parcial
    return resultado  



py5.run_sketch()
