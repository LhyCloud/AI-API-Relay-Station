/* ---------------- sha-256 / 口令派生 ---------------- */

function sha256Hex(str){
  var K=[0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
         0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
         0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
         0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
         0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
         0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
         0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
         0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2];
  var H=[0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19];
  function ror(x,n){return ((x>>>n)|(x<<(32-n)))>>>0;}
  var bytes=[];
  for(var i=0;i<str.length;i++){
    var c=str.charCodeAt(i);
    if(c<0x80) bytes.push(c);
    else if(c<0x800){ bytes.push(0xc0|(c>>6),0x80|(c&63)); }
    else if(c<0xd800||c>=0xe000){ bytes.push(0xe0|(c>>12),0x80|((c>>6)&63),0x80|(c&63)); }
    else {
      i++;
      var c2=str.charCodeAt(i);
      var cp=0x10000+((c-0xd800)<<10)+(c2-0xdc00);
      bytes.push(0xf0|(cp>>18),0x80|((cp>>12)&63),0x80|((cp>>6)&63),0x80|(cp&63));
    }
  }
  var l=bytes.length;
  bytes.push(0x80);
  while(bytes.length%64!==56) bytes.push(0);
  var hi=Math.floor(l/0x20000000), lo=(l*8)>>>0;
  bytes.push((hi>>>24)&255,(hi>>>16)&255,(hi>>>8)&255,hi&255);
  bytes.push((lo>>>24)&255,(lo>>>16)&255,(lo>>>8)&255,lo&255);
  var w=new Array(64);
  for(var off=0;off<bytes.length;off+=64){
    for(var t=0;t<16;t++){
      w[t]=((bytes[off+t*4]<<24)|(bytes[off+t*4+1]<<16)|(bytes[off+t*4+2]<<8)|bytes[off+t*4+3])>>>0;
    }
    for(var t2=16;t2<64;t2++){
      var x=w[t2-15], y=w[t2-2];
      var s0=(ror(x,7)^ror(x,18)^(x>>>3))>>>0;
      var s1=(ror(y,17)^ror(y,19)^(y>>>10))>>>0;
      w[t2]=(w[t2-16]+s0+w[t2-7]+s1)>>>0;
    }
    var a=H[0],b=H[1],c3=H[2],d=H[3],e=H[4],f=H[5],g=H[6],h=H[7];
    for(var t3=0;t3<64;t3++){
      var S1=(ror(e,6)^ror(e,11)^ror(e,25))>>>0;
      var ch=((e&f)^((~e)&g))>>>0;
      var t1=(h+S1+ch+K[t3]+w[t3])>>>0;
      var S0=(ror(a,2)^ror(a,13)^ror(a,22))>>>0;
      var mj=((a&b)^(a&c3)^(b&c3))>>>0;
      var t4=(S0+mj)>>>0;
      h=g;g=f;f=e;e=(d+t1)>>>0;d=c3;c3=b;b=a;a=(t1+t4)>>>0;
    }
    H[0]=(H[0]+a)>>>0; H[1]=(H[1]+b)>>>0; H[2]=(H[2]+c3)>>>0; H[3]=(H[3]+d)>>>0;
    H[4]=(H[4]+e)>>>0; H[5]=(H[5]+f)>>>0; H[6]=(H[6]+g)>>>0; H[7]=(H[7]+h)>>>0;
  }
  var out='';
  for(var k=0;k<8;k++) out+=('00000000'+H[k].toString(16)).slice(-8);
  return out;
}

function kdfHash(saltHex,password){
  var h=sha256Hex(saltHex+password);
  for(var i=0;i<2000;i++) h=sha256Hex(h+saltHex);
  return h;
}

