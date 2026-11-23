"""Valida procedencia y oráculos antes de ejecutar una campaña costosa.

Los casos adversariales evitan confundir un manifest alterado con mediciones
válidas. Las ejecuciones reales de los quince casos se conservan externamente;
esta suite comprueba las guardas sin simular el compilador ni el procesador.
"""
from pathlib import Path
import copy
import importlib.util
import json
import sys
import tempfile
import unittest
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/vliw'))
import evaluar_entrega_final as ev

class EvaluacionFinalTests(unittest.TestCase):
    # Cada manifest mutado es temporal; el archivo de referencia queda intacto.
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(prefix='eval-final-test-');self.addCleanup(self.tmp.cleanup)
        self.dir=Path(self.tmp.name);self.original=json.loads((ROOT/'tests/vliw/programas_finales/manifest.json').read_text())

    def archivo(self,m):
        p=self.dir/'manifest.json';p.write_text(json.dumps(m));return p

    def test_01_manifest_real(self):
        m=ev.leer_manifest(self.archivo(self.original));self.assertEqual(len(m['casos']),15)
        self.assertEqual({c:sum(x['categoria']==c for x in m['casos']) for c in ev.CATEGORIAS},dict.fromkeys(ev.CATEGORIAS,3))

    # Repetir IDs o categorías permitiría rellenar falsamente la cuota de quince.
    def test_02_identidades_y_cuotas(self):
        for cambio in ('id','categoria'):
            m=copy.deepcopy(self.original);m['casos'][0][cambio]=m['casos'][4][cambio]
            with self.assertRaises(ValueError):ev.leer_manifest(self.archivo(m))

    def test_03_fuente_o_hash_alterado(self):
        for clave,valor in [('sha256','0'*64),('fuente','/etc/passwd')]:
            m=copy.deepcopy(self.original);m['casos'][0][clave]=valor
            with self.assertRaises(ValueError):ev.leer_manifest(self.archivo(m))

    # Un bool no puede servir de dirección ni de valor aritmético acreditado.
    def test_04_observables_y_offsets(self):
        for campo,valor in [('observables',True),('offsets_o0',2),('offsets_o1',-4)]:
            m=copy.deepcopy(self.original);k=next(iter(m['casos'][0][campo]));m['casos'][0][campo][k]=valor
            with self.assertRaises(ValueError):ev.leer_manifest(self.archivo(m))

    def test_05_esquema(self):
        for version in (True,2,'1'):
            m=copy.deepcopy(self.original);m['version']=version
            with self.assertRaises(ValueError):ev.leer_manifest(self.archivo(m))

    # Un ID inexistente falla antes de compilar, crear evidencia o invocar RTL.
    def test_06_id_sin_publicacion(self):
        d=self.dir/'no-publicado'
        with self.assertRaises(ValueError):ev.evaluar('/no/balc','/no/arquitectura',d,self.archivo(self.original),['inventado'])
        self.assertFalse(d.exists())

    def test_07_salida_ocupada(self):
        marker=self.dir/'conservar.txt';marker.write_text('conservar')
        with self.assertRaises(ValueError):ev.evaluar('/no/balc','/no/arquitectura',self.dir,self.archivo(self.original))
        self.assertEqual(marker.read_text(),'conservar')

    # Un trap público se conserva como dato; nunca se convierte en cero para
    # hacer coincidir una ejecución. Marcadores truncados o repetidos fallan.
    def test_08_estado_publico(self):
        linea='[ARCH_STATE] crypto=2 traps=1 trap_pc=48'
        self.assertEqual(ev.estado_publico(linea),dict(crypto=2,traps=1,trap_pc=48))
        for texto in ('',linea+'\n'+linea,'[ARCH_STATE] crypto=2 traps=0'):
            with self.assertRaises(ValueError):ev.estado_publico(texto)

if __name__=='__main__':unittest.main()
