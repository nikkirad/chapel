use HashedDist;
use IO, ChplFormat;

record MyMapper {
  proc this(ind, targetLocs: [] locale) {
    const numlocs = targetLocs.domain.size;
    const indAsInt = ind: int;
    return indAsInt % numlocs;
  }
}

var D: domain(int) dmapped Hashed(idxType=int, mapper=new MyMapper());
D += 0;
D += 1;

stdout.withSerializer(ChplSerializer).writef("%?\n", D);
