import json
import string

data = json.load(open('quotes.json'))
alphabet = list(string.ascii_lowercase)

quotes = []


for quote in data['quotes']:
  if all(x not in alphabet for x in quote['text']):
    continue
  quotes.append(quote)

with open('trimmed_quotes.json', 'w') as f:
    f.write(json.dumps(quotes))