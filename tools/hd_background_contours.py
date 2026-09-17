"""Reconstruct source alpha for private HD scenery; no game-state changes."""
import numpy as np

def contour(alpha):
    """Offline scalar version of the host's slope reconstruction, without dithering.

    Keep source sampling periodic only in the supplied context; the central
    crop is eight pixels from its border, beyond the three-pixel stencil.
    """
    a = alpha.astype(np.float32)/255
    h,w=a.shape
    result=np.empty((h*4,w*4),np.uint8)
    for iy in range(4):
        for ix in range(4):
            fx=abs((ix+.5)/4-.5)+.5; fy=abs((iy+.5)/4-.5)+.5
            sx=-1 if ix<2 else 1; sy=-1 if iy<2 else 1
            def s(x,y): return np.roll(a,(-y*sy,-x*sx),axis=(0,1))
            E=a; B=s(0,-1); C=s(1,-1); D=s(-1,0); F=s(1,0)
            G=s(-1,1); H=s(0,1); I=s(1,1); F4=s(2,0); H5=s(0,2); I4=s(2,1); I5=s(1,2)
            df=lambda x,y: np.abs(x-y)
            eq=lambda x,y: df(x,y)<.002
            band=1.6; aa=4/band
            ax=.5*np.clip((fx-(1-.5*band/4))*4/band,0,1)
            ay=.5*np.clip((fy-(1-.5*band/4))*4/band,0,1)
            base=((1-ax)*E+ax*F)*(1-ay)+((1-ax)*H+ax*I)*ay
            wd1=df(E,C)+df(E,G)+df(I,H5)+df(I,F4)+4*df(H,F)
            wd2=df(H,D)+df(H,I5)+df(F,I4)+df(F,B)+4*df(E,I)
            edge=(wd1<wd2)&~eq(E,H)&~eq(E,F)&~(eq(E,I)&eq(H,F))
            nc=np.where(df(E,F)<=df(E,H),F,H)
            cov=np.full(a.shape,np.clip((fx+fy-1.5)*aa+.5,0,1),np.float32)
            left=(2*df(F,G)<=df(H,C))&~eq(E,G)&~eq(D,G)
            up=(df(F,G)>=2*df(H,C))&~eq(E,C)&~eq(B,C)
            cov=np.where(left,np.maximum(cov,np.clip((2*fx+fy-2)*aa*.75+.5,0,1)),cov)
            cov=np.where(up,np.maximum(cov,np.clip((fx+2*fy-2)*aa*.75+.5,0,1)),cov)
            left3=left&(4*df(F,G)<=df(H,C))&~eq(E,s(-2,1))&~eq(s(-2,0),s(-2,1))
            up3=up&(df(F,G)>=4*df(H,C))&~eq(E,s(1,-2))&~eq(s(0,-2),s(1,-2))
            cov=np.where(left3,np.maximum(cov,np.clip((3*fx+fy-2.5)*aa*.6+.5,0,1)),cov)
            cov=np.where(up3,np.maximum(cov,np.clip((fx+3*fy-2.5)*aa*.6+.5,0,1)),cov)
            result[iy::4,ix::4]=np.rint(np.clip(np.where(edge,base*(1-cov)+nc*cov,base),0,1)*255).astype(np.uint8)
    return result
