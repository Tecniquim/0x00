#!/home/villares/miniconda3/envs/py5coding/bin/python

# Requires pdfrw2 - https://pypi.org/project/pdfrw2/

import sys
from pdfrw import PdfReader, PdfWriter, PageMerge
    
args = sys.argv
if len(args) > 2:
    try: 
        base_file, overlay_file = args[1:3]
        if len(args) == 4:
            output_file = args[3]
        else:
            output_file = 'output.pdf'
        base_pages = PdfReader(base_file).pages
        overlay_pages = PdfReader(overlay_file).pages
    except:
        print('Needs 2 PDF files as input arguments. Output filename is optional.')
        exit()
    writer = PdfWriter(output_file)    
    for i, page in enumerate(base_pages):
        overlay = overlay_pages[i % len(overlay_pages)]
        new_page = PageMerge() + page + overlay
        writer.addpage(new_page.render())
    writer.write()
else:
    print(
        'Usage:\n'
        '<base file a> <overlay file b> [<output file>]\n'
        'Note: If the overlay file is shorter, its pages will be used repeatedly'
        )
        
