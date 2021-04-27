import alles
from time import sleep
clients = list(alles.sync().keys())
try:
    for i in range(400): 
        alles.send(wave=alles.FM,
            note=[[60,58][(i//32)%2],[48,52][(i//48)%2]][(i//64)%2]+[0,3,5,7,10,11][i%6]*2,
            vel=0.1*(i%9),
            patch=14+i%2,
            client=clients[i%len(clients)])
        sleep([0.08,0.05,0.1][i%3]*(i%3))
except KeyboardInterrupt:
    pass
alles.reset()

