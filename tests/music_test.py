import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import contextlib
import io

spec = importlib.util.spec_from_file_location('music', Path(__file__).resolve().parents[1] / 'tools/music.py')
music = importlib.util.module_from_spec(spec)
spec.loader.exec_module(music)

class MusicTest(unittest.TestCase):
    def test_playlist_metadata_only(self):
        data = {'props': {'pageProps': {'state': {'data': {'entity': {'trackList': [
            {'title': 'Song\nTitle', 'subtitle': 'Artist', 'audioPreview': {'url': 'must-not-play'}}
        ]}}}}}}
        rows = music.spotify_rows('<script id="__NEXT_DATA__">' + json.dumps(data) + '</script>')
        self.assertEqual(rows, [('query', 'Artist Song Title', 'Song Title', 'Artist')])

    def test_only_public_playlist_urls(self):
        good = 'https://open.spotify.com/playlist/37i9dQZF1DXcBWIGoYBM5M?si=abc'
        self.assertEqual(music.spotify_id(good), '37i9dQZF1DXcBWIGoYBM5M')
        for url in ['http://open.spotify.com/playlist/abc', 'https://open.spotify.com.evil/playlist/37i9dQZF1DXcBWIGoYBM5M', 'file:///etc/passwd']:
            self.assertIsNone(music.spotify_id(url))

    def test_cache_and_tsv_are_bounded(self):
        with tempfile.TemporaryDirectory() as root, patch.object(music, 'CACHE', Path(root)), patch.object(music, 'search', return_value=[('video', 'abcdefghijk', 'Title\tInjection', 'Artist')]) as search:
            for query in ['one', 'two', 'three', 'four', 'four']:
                with contextlib.redirect_stdout(io.StringIO()) as out:
                    music.run(['search', query])
                self.assertEqual(len(out.getvalue().strip().split('\t')), 4)
            self.assertEqual(search.call_count, 4)
            self.assertEqual(len(list(Path(root).glob('*.json'))), 3)

    def test_recommendations_skip_history_and_invalid_ids(self):
        with tempfile.TemporaryDirectory() as root, patch.object(music, 'CACHE', Path(root)):
            (music.CACHE/'history.txt').write_text(json.dumps(['abcdefghijk', 'lmnopqrstuv']))
            from unittest.mock import MagicMock
            client = MagicMock()
            client.__enter__.return_value.extract_info.return_value = {'entries': iter([
                None, {'id':'invalid'}, {'id':'abcdefghijk'}, {'id':'lmnopqrstuv'}, {'id':'12345678901'}])}
            with patch.object(music, 'youtube_client', return_value=client), patch.object(music, 'resolve') as resolve:
                music.run(['next'])
                resolve.assert_called_once_with('video', '12345678901')
                query=client.__enter__.return_value.extract_info.call_args.args[0]
                self.assertEqual(query, 'https://www.youtube.com/watch?v=lmnopqrstuv&list=RDlmnopqrstuv')

    def test_resolve_records_bounded_history_and_signed_stream(self):
        from unittest.mock import MagicMock
        client=MagicMock()
        client.__enter__.return_value.extract_info.return_value={
            'id':'abcdefghijk','url':'https://example.test/audio?'+'x'*12000,'title':'Test song'}
        with tempfile.TemporaryDirectory() as root, patch.object(music, 'CACHE', Path(root)), patch.object(music, 'youtube_client', return_value=client):
            (music.CACHE/'history.txt').write_text(json.dumps(['12345678901']*60))
            music.run(['resolve','video','abcdefghijk'])
            history=json.loads((music.CACHE/'history.txt').read_text())
            self.assertEqual(len(history), 50)
            self.assertEqual(history[-1], 'abcdefghijk')
            self.assertGreater(len((music.CACHE/'stream.txt').read_text()), 12000)

    def test_no_recommendation_stops_instead_of_repeating(self):
        from unittest.mock import MagicMock
        client=MagicMock()
        client.__enter__.return_value.extract_info.return_value={'entries':[{'id':'abcdefghijk'}]*30}
        with tempfile.TemporaryDirectory() as root, patch.object(music, 'CACHE', Path(root)), patch.object(music, 'youtube_client', return_value=client):
            (music.CACHE/'history.txt').write_text('["abcdefghijk"]')
            with self.assertRaisesRegex(ValueError, 'No new'):
                music.run(['next'])

    def test_unavailable_playlist_has_clear_error(self):
        with self.assertRaisesRegex(ValueError, 'public playlist'):
            music.spotify_rows('<html>Sign in</html>')

if __name__ == '__main__':
    unittest.main()
