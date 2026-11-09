"""Prints the text column of Wikipedia parquet shards, one paragraph per line.

Usage: python extract.py <max characters> <shard.parquet>...
"""
import sys

import pyarrow.parquet as pq


def main():
    limit = int(sys.argv[1])
    written = 0
    for shard in sys.argv[2:]:
        for batch in pq.ParquetFile(shard).iter_batches(columns=["text"], batch_size=1000):
            for text in batch.column(0).to_pylist():
                for paragraph in text.split("\n"):
                    if paragraph:
                        sys.stdout.write(paragraph + "\n")
                        written += len(paragraph)
                if written >= limit:
                    return


if __name__ == "__main__":
    main()
